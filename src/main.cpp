#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
#include <cstring>
#include <unordered_set>
#include <mutex>

using namespace geode::prelude;

class $modify(MyLevelInfoLayer, LevelInfoLayer)
{
    inline static std::unordered_set<MyLevelInfoLayer *> s_liveInstances;
    inline static std::mutex s_liveMutex;
    static void registerLive(MyLevelInfoLayer *ptr)
    {
        std::scoped_lock _{s_liveMutex};
        s_liveInstances.insert(ptr);
    }
    static void unregisterLive(MyLevelInfoLayer *ptr)
    {
        std::scoped_lock _{s_liveMutex};
        s_liveInstances.erase(ptr);
    }
    static bool isLive(MyLevelInfoLayer *ptr)
    {
        std::scoped_lock _{s_liveMutex};
        return s_liveInstances.find(ptr) != s_liveInstances.end();
    }
    static bool anyLive()
    {
        std::scoped_lock _{s_liveMutex};
        return !s_liveInstances.empty();
    }
    static bool hasActiveForSong(int songID)
    {
        std::scoped_lock _{s_liveMutex};
        for (auto ptr : s_liveInstances)
        {
            if (!ptr)
                continue;
            if (ptr->m_fields->m_isActive && ptr->m_fields->m_currentLevelSongID == songID)
                return true;
        }
        return false;
    }
    static bool hasActiveForTrack(int trackId)
    {
        std::scoped_lock _{s_liveMutex};
        for (auto ptr : s_liveInstances)
        {
            if (!ptr)
                continue;
            auto lvl = ptr->m_fields->m_currentLevel;
            if (ptr->m_fields->m_isActive && lvl && lvl->m_audioTrack == trackId)
                return true;
        }
        return false;
    }

    // i hate this code so much, i freaking didnt plan before making this mod. i just notice how yucky the codebase has become.
    // this is absolutely ass and hacky way, kids dont do this

    struct Fields
    {
        float m_originalVolume = 0.0f;
        int m_currentLevelSongID = 0;
        int m_bgMusicTime = 0;
        void *m_musicDelegate = nullptr;
        bool m_hasInitialized = false;
        GJGameLevel *m_currentLevel = nullptr;
        bool m_isActive = false;
        bool m_isChecking = false;
    };
    // handles the download callback forwarding i think?
    struct SongDownloadForwarder : MusicDownloadDelegate
    {
        MyLevelInfoLayer *owner = nullptr;
        explicit SongDownloadForwarder(MyLevelInfoLayer *o) : owner(o) {}
        void downloadSongFinished(int songID) override
        {
            // Don't call owner directly from the download thread. Queue to main thread.
            MyLevelInfoLayer *own = owner;
            Loader::get()->queueInMainThread([own, songID]()
                                             {
                if (own && MyLevelInfoLayer::isLive(own))
                    own->onSongDownloaded(songID); });
        }
    };

    void playSongWithOptionalMid(const std::string &path, int lengthIdOrOne, float fadeTime, bool playMid, const char *kind)
    {
        auto fmod = FMODAudioEngine::sharedEngine();
        fmod->stopAllMusic(true);
        fmod->playMusic(path, true, fadeTime, 1);
        m_fields->m_isActive = true;

        if (!playMid)
        {
            log::info("Playing {} song from start: {}", kind, path);
            return;
        }

        // Set position to middle using a delayed approach
        MyLevelInfoLayer *self = this;
        Loader::get()->queueInMainThread([self, fadeTime, path, lengthIdOrOne, kind]()
                                         {
            if (!MyLevelInfoLayer::isLive(self))
                return;
            if (!self->m_fields->m_isActive)
                return;
            auto audioEngine = FMODAudioEngine::sharedEngine();
            if (auto channelGroup = audioEngine->m_backgroundMusicChannel)
            {
                unsigned int middleMs = audioEngine->getMusicLengthMS(lengthIdOrOne) / 2;
                FMOD::Channel *channel = nullptr;
                auto fmod = FMODAudioEngine::sharedEngine();
                auto result = channelGroup->getChannel(0, &channel);
                if (result == FMOD_OK && channel)
                {
                    auto setResult = channel->setPosition(middleMs, 1);
                    fmod->playMusic(path, true, fadeTime, 1);
                    if (std::strcmp(kind, "Built-in") == 0)
                    {
                        Mod::get()->setSavedValue("levelMusicPosition", static_cast<int>(middleMs));
                        log::info("({}) Channel position set result: {}, applied & saved middle position: {} ms", kind, (int)setResult, middleMs);
                    }
                    else
                    {
                        log::info("({}) Channel position set result: {}, applied middle position (savedPos untouched): {} ms", kind, (int)setResult, middleMs);
                    }
                }
                else
                {
                    log::warn("({}) Failed to get channel from group, result: {}", kind, (int)result);
                }
            }
        });
    }

    void stopCheckMusicAndRetry()
    {
        m_fields->m_isChecking = false;
    }

    void onSongDownloaded(int songID)
            {
                auto level = m_fields->m_currentLevel;
                if (!level)
                    return;
                if (songID != m_fields->m_currentLevelSongID)
                    return;

                // Stop polling once we know the song is available
                stopCheckMusicAndRetry();

                // If music is already active (e.g., from a previous trigger), avoid double-playing
                if (m_fields->m_isActive)
                    return;

                auto musicManager = MusicDownloadManager::sharedState();
                auto songPath = musicManager->pathForSong(songID);
                if (songPath.empty())
                    return;

                // @geode-ignore(unknown-setting)
                float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                log::info("downloadSongFinished: playing custom song {} for songID {}", songPath, songID);

                auto fmod = FMODAudioEngine::sharedEngine();
                fmod->stopAllMusic(true);
                playSongWithOptionalMid(songPath, 1, fadeTime, playMid, "Custom");
            }

            void onExitTransitionDidStart()
            {
                // layer is dead, unregister, killed, L taken
                unregisterLive(this);

                m_fields->m_isActive = false;

                // stop polling on exit
                stopCheckMusicAndRetry();

                // Remove delegate to avoid late callbacks after the layer is leaving
                if (m_fields->m_musicDelegate)
                {
                    auto forwarder = static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
                    // remove from manager first
                    MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(forwarder);
                    // clear owner so any in-flight callbacks won't dereference the layer
                    forwarder->owner = nullptr;
                    m_fields->m_musicDelegate = nullptr;
                    // schedule deletion on main thread to avoid deleting while manager may be dispatching
                    Loader::get()->queueInMainThread([forwarder]()
                                                     { delete forwarder; });
                }

                // no play layer? means we're exiting back to menu, not entering PlayLayer
                if (!PlayLayer::get())
                {
                    auto fmod = FMODAudioEngine::sharedEngine();
                    auto gm = GameManager::sharedState();

                    // Stop any music currently playing
                    fmod->stopAllMusic(true);
                    log::info("Leaving LevelInfoLayer, level music stopped");

                    // Always play menu music when exiting to the menu
                    log::info("Exiting to menu, playing menu music");
                    gm->playMenuMusic();
                }

                LevelInfoLayer::onExitTransitionDidStart();
            }

            void onPlay(CCObject * sender)
            {
                if (!m_fields->m_isActive)
                {
                    stopCheckMusicAndRetry();
                    if (m_fields->m_musicDelegate)
                    {
                        auto forwarder = static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
                        MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(forwarder);
                        forwarder->owner = nullptr;
                        m_fields->m_musicDelegate = nullptr;
                        Loader::get()->queueInMainThread([forwarder]()
                                                         { delete forwarder; });
                    }
                    LevelInfoLayer::onPlay(sender);
                    return;
                }

                auto fmod = FMODAudioEngine::sharedEngine();
                m_fields->m_isActive = false;
                stopCheckMusicAndRetry();

                if (m_fields->m_musicDelegate)
                {
                    auto forwarder = static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
                    MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(forwarder);
                    forwarder->owner = nullptr;
                    m_fields->m_musicDelegate = nullptr;
                    Loader::get()->queueInMainThread([forwarder]()
                                                     { delete forwarder; });
                }
                log::info("onPlay triggered - stopping level music");
                fmod->stopAllMusic(true);
                fmod->stopAllMusic(false);
                log::info("Music stopped");
                LevelInfoLayer::onPlay(sender);
            }

            void onBack(CCObject * sender)
            {
                if (!m_fields->m_isActive)
                {
                    stopCheckMusicAndRetry();
                    unregisterLive(this);
                    LevelInfoLayer::onBack(sender);
                    return;
                }

                // stop music and play menu music if active
                auto fmod = FMODAudioEngine::sharedEngine();
                auto gm = GameManager::sharedState();
                fmod->stopAllMusic(true);
                gm->playMenuMusic();
                log::info("onBack triggered - stopping level music & play menu music");
                stopCheckMusicAndRetry();
                // remove delegate
                if (m_fields->m_musicDelegate)
                {
                    {
                        auto forwarder = static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
                        MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(forwarder);
                        forwarder->owner = nullptr;
                        m_fields->m_musicDelegate = nullptr;
                        Loader::get()->queueInMainThread([forwarder]()
                                                         { delete forwarder; });
                    }
                    // restore the background music position if available
                    {
                        auto audioEngine = FMODAudioEngine::sharedEngine();
                        if (audioEngine && audioEngine->m_backgroundMusicChannel)
                        {
                            FMOD::Channel *channel = nullptr;
                            auto result = audioEngine->m_backgroundMusicChannel->getChannel(0, &channel);
                            if (result == FMOD_OK && channel)
                            {
                                channel->setPosition(static_cast<unsigned int>(m_fields->m_bgMusicTime), (FMOD_TIMEUNIT)1);
                                log::debug("Restored background music time: {} ms", m_fields->m_bgMusicTime);
                            }
                        }
                    }
                    // reset the bgMusicTime to 0
                    m_fields->m_bgMusicTime = 0;
                }
                LevelInfoLayer::onBack(sender);
            }

            ~MyLevelInfoLayer()
            {
                // commit deconstruction myself
                stopCheckMusicAndRetry();
                unregisterLive(this);
                if (m_fields->m_musicDelegate)
                {
                    {
                        auto forwarder = static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
                        MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(forwarder);
                        forwarder->owner = nullptr;
                        m_fields->m_musicDelegate = nullptr;
                        Loader::get()->queueInMainThread([forwarder]()
                                                         { delete forwarder; });
                    }
                }
            }
};

// playlayer stuff
class $modify(MyPlayLayer, PlayLayer)
{
            bool init(GJGameLevel * level, bool useReplay, bool dontCreateObjects)
            {
                // Stop all music before initializing the play layer
                auto fmod = FMODAudioEngine::sharedEngine();
                log::info("PlayLayer init - ensuring music is stopped");
                fmod->stopAllMusic(true);

                return PlayLayer::init(level, useReplay, dontCreateObjects);
            }

            void onQuit()
            {
                auto level = this->m_level;
                PlayLayer::onQuit();

                Loader::get()->queueInMainThread([level]()
                {
                    auto fmod = FMODAudioEngine::sharedEngine();
                    auto gm = GameManager::sharedState();
                    auto musicManager = MusicDownloadManager::sharedState();

                    if (!fmod)
                        return;

                    int savedPos = 0;
                    if (Mod::get()->hasSavedValue("levelMusicPosition"))
                    {
                        savedPos = Mod::get()->getSavedValue<int>("levelMusicPosition");
                    }

                    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");

                    if (!MyLevelInfoLayer::anyLive())
                    {
                        if (level)
                        {
                            auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
                            if (!audioPath.empty())
                            {
                                auto resourcePath = (geode::dirs::getResourcesDir() / std::string(audioPath));
                                log::info("No LevelInfoLayer present after PlayLayer quit; restoring built-in track: {}", level->m_audioTrack);
                                fmod->playMusic(gd::string(resourcePath.string()), true, fadeTime, level->m_audioTrack);

                                if (savedPos > 0)
                                {
                                    Loader::get()->queueInMainThread([savedPos]()
                                    {
                                        auto fmod = FMODAudioEngine::sharedEngine();
                                        if (!fmod || !fmod->m_backgroundMusicChannel)
                                            return;
                                        FMOD::Channel *channel = nullptr;
                                        auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
                                        if (result == FMOD_OK && channel)
                                        {
                                            channel->setPosition(static_cast<unsigned int>(savedPos), (FMOD_TIMEUNIT)1);
                                            log::info("Re-applied saved built-in song position: {} ms", savedPos);
                                        }
                                    });
                                }
                                return;
                            }
                        }

                        log::info("No LevelInfoLayer present after PlayLayer quit; playing menu music instead");
                        if (gm)
                            gm->playMenuMusic();
                        return;
                    }

                    fmod->stopAllMusic(true);

                    if (level && level->m_songID != 0)
                    {
                        if (musicManager && musicManager->isSongDownloaded(level->m_songID))
                        {
                            auto songPath = musicManager->pathForSong(level->m_songID);
                            log::info("Restoring custom song after PlayLayer quit: {}", songPath);
                            fmod->playMusic(songPath, true, fadeTime, 1);

                            if (savedPos > 0)
                            {
                                Loader::get()->queueInMainThread([savedPos]()
                                {
                                    auto fmod = FMODAudioEngine::sharedEngine();
                                    if (!fmod || !fmod->m_backgroundMusicChannel)
                                        return;

                                    FMOD::Channel *channel = nullptr;
                                    auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
                                    if (result == FMOD_OK && channel)
                                    {
                                        channel->setPosition(Mod::get()->getSavedValue<int>("levelMusicPosition"), (FMOD_TIMEUNIT)1);
                                        log::info("Re-applied saved custom song position: {} ms", Mod::get()->getSavedValue<int>("levelMusicPosition"));
                                    }
                                    else
                                    {
                                        log::warn("Failed to re-apply custom song position, result: {}", (int)result);
                                    }
                                });
                            }
                            return;
                        }

                        log::info("Custom song missing after PlayLayer quit, falling back to menu music");
                    }

                    if (level)
                    {
                        auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
                        if (!audioPath.empty())
                        {
                            auto resourcePath = (geode::dirs::getResourcesDir() / std::string(audioPath));
                            log::info("Restoring built-in song after PlayLayer quit: {}", level->m_audioTrack);
                            fmod->playMusic(gd::string(resourcePath.string()), true, fadeTime, level->m_audioTrack);

                            if (savedPos > 0)
                            {
                                Loader::get()->queueInMainThread([savedPos]()
                                {
                                    auto fmod = FMODAudioEngine::sharedEngine();
                                    if (!fmod || !fmod->m_backgroundMusicChannel)
                                        return;

                                    FMOD::Channel *channel = nullptr;
                                    auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
                                    if (result == FMOD_OK && channel)
                                    {
                                        channel->setPosition(static_cast<unsigned int>(savedPos), (FMOD_TIMEUNIT)1);
                                        log::info("Re-applied saved built-in song position: {} ms", savedPos);
                                    }
                                });
                            }
                            return;
                        }
                    }

                    if (gm)
                    {
                        log::info("No level music to restore, playing menu music");
                        gm->playMenuMusic();
                    }
                });
            }
    };