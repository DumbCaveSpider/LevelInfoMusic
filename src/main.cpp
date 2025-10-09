#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <filesystem>
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
            if (owner && MyLevelInfoLayer::isLive(owner))
                owner->onSongDownloaded(songID);
        }
    };

    void playCustomSong(const std::string &songPath, float fadeTime, bool playMid)
    {
        auto fmod = FMODAudioEngine::sharedEngine();
        fmod->stopAllMusic(true);
        fmod->playMusic(songPath, true, fadeTime, 1);
        m_fields->m_isActive = true;

        if (!playMid)
        {
            log::info("Playing custom song from start: {}", songPath);
            return;
        }

        // Set position to middle using a delayed approach
        MyLevelInfoLayer *self = this;
        Loader::get()->queueInMainThread([self, fadeTime, songPath]()
                                         {
            if (!MyLevelInfoLayer::isLive(self)) return;
            if (!self->m_fields->m_isActive) return;
            auto audioEngine = FMODAudioEngine::sharedEngine();
            if (auto channelGroup = audioEngine->m_backgroundMusicChannel) {
                unsigned int middleMs = audioEngine->getMusicLengthMS(1) / 2;
                FMOD::Channel* channel = nullptr;
                auto fmod = FMODAudioEngine::sharedEngine();
                auto result = channelGroup->getChannel(0, &channel);
                if (result == FMOD_OK && channel) {
                    auto setResult = channel->setPosition(middleMs, 1);
                    fmod->playMusic(songPath, true, fadeTime, 1);
                    log::info("(Custom) Channel position set result: {}", (int)setResult);
                } else {
                    log::warn("(Custom) Failed to get channel from group, result: {}", (int)result);
                }
            } });
        log::info("Playing custom song from middle");
    }

    // polling loop to detect when the custom song has finished downloading
    void startCheckMusicAndRetry()
    {
        if (m_fields->m_isChecking)
            return;
        m_fields->m_isChecking = true;
        this->schedule(schedule_selector(MyLevelInfoLayer::checkMusicAndRetry), 0.1f);
        log::debug("Started checkMusicAndRetry polling");
    }

    void stopCheckMusicAndRetry()
    {
        if (!m_fields->m_isChecking)
            return;
        this->unschedule(schedule_selector(MyLevelInfoLayer::checkMusicAndRetry));
        m_fields->m_isChecking = false;
        log::debug("Stopped checkMusicAndRetry polling");
    }

    void checkMusicAndRetry(float)
    {
        auto level = m_fields->m_currentLevel;
        if (!level)
            return;

        // already started playing something for this layer, stop polling
        if (m_fields->m_isActive)
        {
            stopCheckMusicAndRetry();
            return;
        }

        if (level->m_songID != 0)
        {
            auto musicManager = MusicDownloadManager::sharedState();
            if (musicManager->isSongDownloaded(level->m_songID))
            {
                log::info("Polling detected custom song downloaded (ID: {}), playing now", level->m_songID);
                stopCheckMusicAndRetry();
                initializeLevelMusic();
            }
        }
    }

    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        registerLive(this);

        m_fields->m_currentLevel = level;
        m_fields->m_currentLevelSongID = level ? level->m_songID : 0;
        m_fields->m_hasInitialized = true;

        log::debug("levelinfo says hi! level song ID: {}", level->m_songID);

        // Save the background music position
        {
            auto audioEngine = FMODAudioEngine::sharedEngine();
            unsigned int posMs = 0;
            if (audioEngine && audioEngine->m_backgroundMusicChannel)
            {
                FMOD::Channel *ch = nullptr;
                auto res = audioEngine->m_backgroundMusicChannel->getChannel(0, &ch);
                if (res == FMOD_OK && ch)
                {
                    ch->getPosition(&posMs, (FMOD_TIMEUNIT)1);
                }
            }
            m_fields->m_bgMusicTime = static_cast<int>(posMs);
            log::debug("Saved background music time: {} ms", m_fields->m_bgMusicTime);
        }

        // Register delegate forwarder once
        if (!m_fields->m_musicDelegate)
        {
            auto forwarder = new SongDownloadForwarder(this);
            m_fields->m_musicDelegate = forwarder;
            MusicDownloadManager::sharedState()->addMusicDownloadDelegate(forwarder);
        }

        // check if the custom music is not downloaded, start polling (but don't stop menu music)
        auto musicManager = MusicDownloadManager::sharedState();
        if (level->m_songID != 0 && !musicManager->isSongDownloaded(level->m_songID))
        {
            log::info("Custom song not downloaded yet, keeping menu music and starting polling");
            startCheckMusicAndRetry();
            return true;
        }

        // Stop menu music only when we're ready to play level music
        auto fmod = FMODAudioEngine::sharedEngine();
        fmod->stopAllMusic(true);
        log::info("Stopped menu music on LevelInfoLayer init");

        initializeLevelMusic();
        return true;
    }

    void initializeLevelMusic()
    {
        auto level = m_fields->m_currentLevel;
        if (!level)
            return;

        auto musicManager = MusicDownloadManager::sharedState();

        // @geode-ignore(unknown-setting)
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");

        m_fields->m_currentLevelSongID = level->m_songID;

        auto fmod = FMODAudioEngine::sharedEngine();
        m_fields->m_originalVolume = fmod->getBackgroundMusicVolume();

        if (level->m_songID != 0 && musicManager->isSongDownloaded(level->m_songID))
        {
            log::info("LevelInfoLayer: custom song requested (ID: {})", level->m_songID);
            auto songPath = musicManager->pathForSong(level->m_songID);
            fmod->stopAllMusic(true);
            playCustomSong(songPath, fadeTime, playMid);
        }
        else
        {
            // Built-in audio track
            fmod->stopAllMusic(true);
            auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (!audioPath.empty())
            {
                auto resourcePath = (geode::dirs::getResourcesDir() / std::string(audioPath));
                log::info("Level uses built-in audio track: {}, from path: {}", level->m_audioTrack, resourcePath.string());
                fmod->playMusic(gd::string(resourcePath.string()), true, fadeTime, level->m_audioTrack);
                m_fields->m_isActive = true;

                if (playMid)
                {
                    int trackId = level->m_audioTrack;
                    MyLevelInfoLayer *self = this;
                    Loader::get()->queueInMainThread([self, trackId, audioPath]()
                                                     {
                        if (!MyLevelInfoLayer::isLive(self)) return;
                        if (!self->m_fields->m_isActive) return;
                        auto audioEngine = FMODAudioEngine::sharedEngine();
                        if (auto channelGroup = audioEngine->m_backgroundMusicChannel) {
                            unsigned int middleMs = audioEngine->getMusicLengthMS(trackId) / 2;
                            FMOD::Channel* channel = nullptr;
                            auto level = self->m_fields->m_currentLevel;
                            auto musicManager = MusicDownloadManager::sharedState();
                            auto fmod = FMODAudioEngine::sharedEngine();
                            float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                            auto resourcePath = (geode::dirs::getResourcesDir() / std::string(audioPath));
                            auto result = channelGroup->getChannel(0, &channel);
                            if (result == FMOD_OK && channel) {
                                auto setResult = channel->setPosition(middleMs, 1);
                                fmod->playMusic(gd::string(resourcePath.string()), true, fadeTime, 1);
                                log::info("(Built-in) Channel position set result: {}", (int)setResult);
                            } else {
                                log::warn("(Built-in) Failed to get channel from group, result: {}", (int)result);
                            }
                        } });
                    log::info("Playing built-in track from middle");
                }
            }
            else
            {
                log::debug("Unknown built-in track ID: {}", level->m_audioTrack);
            }
        }
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
        playCustomSong(songPath, fadeTime, playMid);
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
            MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate));
            delete static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate); // get this dum dum delegate out of here xd
            m_fields->m_musicDelegate = nullptr;
        }

        // no play layer?
        if (!PlayLayer::get())
        {
            auto fmod = FMODAudioEngine::sharedEngine();
            auto gm = GameManager::sharedState();
            float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
            bool playMid = Mod::get()->getSettingValue<bool>("playMid");

            // Stop any music currently playing
            fmod->stopAllMusic(true);
            log::info("Leaving LevelInfoLayer, level music stopped");

            // Check for custom music
            auto level = m_fields->m_currentLevel;
            if (level && level->m_songID != 0)
            {
                auto musicManager = MusicDownloadManager::sharedState();
                if (musicManager->isSongDownloaded(level->m_songID))
                {
                    // no music loop randomizer, i will hijack the fmod channel back >:)
                    auto songPath = musicManager->pathForSong(level->m_songID);
                    FMOD::Channel *hijackChannel = nullptr;
                    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                    fmod->playMusic(songPath, true, fadeTime, 1); // Play instantly, no fade
                    if (fmod->m_backgroundMusicChannel)
                    {
                        auto result = fmod->m_backgroundMusicChannel->getChannel(0, &hijackChannel);
                        if (result == FMOD_OK && hijackChannel)
                        {
                            // set position to middle if playMid is true
                            if (playMid)
                            {
                                unsigned int middleMs = fmod->getMusicLengthMS(1) / 2;
                                hijackChannel->setPosition(middleMs, 1);
                                log::info("Hijacked FMOD channel, set position to middle: {} ms", middleMs);
                            }
                            else
                            {
                                hijackChannel->setPosition(0, 1);
                                log::info("Hijacked FMOD channel, set position to start");
                            }
                        }
                        else
                        {
                            log::warn("Could not hijack FMOD channel, result: {}", (int)result);
                        }
                    }
                    log::info("Forcefully playing custom music on LevelInfoLayer after exit");

                    // Force-stop menu music in case another mod (ehm ehm Menu Loop Randomizer) triggers it
                    fmod->stopAllMusic(false); // stop all music first

                    // re-assert custom music after a short delay
                    MyLevelInfoLayer *self = this;
                    Loader::get()->queueInMainThread([self, songPath, playMid]()
                                                     {
                        if (!MyLevelInfoLayer::isLive(self)) return;
                        auto fmod = FMODAudioEngine::sharedEngine();
                        fmod->playMusic(songPath, true, 0.0f, 1);
                        self->m_fields->m_isActive = true;
                        if (playMid && fmod->m_backgroundMusicChannel)
                        {
                            FMOD::Channel* hijackChannel = nullptr;
                            auto result = fmod->m_backgroundMusicChannel->getChannel(0, &hijackChannel);
                            if (result == FMOD_OK && hijackChannel)
                            {
                                unsigned int middleMs = fmod->getMusicLengthMS(1) / 2;
                                hijackChannel->setPosition(middleMs, 1);
                                log::info("(Delayed) Re-hijacked FMOD channel, set position to middle: {} ms", middleMs);
                            }
                        } });
                }
                else
                {
                    // Play menu music if custom music is not downloaded
                    log::info("Custom song not downloaded, play menu music after exit");
                    gm->playMenuMusic();
                    m_fields->m_isActive = false;
                }
            }
            else
            {
                // Play menu music if no custom song for this level
                log::info("No custom song for this level, play menu music after exit");
                gm->playMenuMusic();
                m_fields->m_isActive = false;
            }
        }

        LevelInfoLayer::onExitTransitionDidStart();
    }

    void onPlay(CCObject *sender)
    {
        if (!m_fields->m_isActive)
        {
            stopCheckMusicAndRetry();
            if (m_fields->m_musicDelegate)
            {
                MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate));
                delete static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
                m_fields->m_musicDelegate = nullptr;
            }
            LevelInfoLayer::onPlay(sender);
            return;
        }

        m_fields->m_isActive = false;
        stopCheckMusicAndRetry();

        if (m_fields->m_musicDelegate)
        {
            MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate));
            delete static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
            m_fields->m_musicDelegate = nullptr;
        }
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("onPlay triggered - stopping level music");
        fmod->stopAllMusic(true);
        fmod->stopAllMusic(false);
        fmod->stopAllEffects();
        log::info("Music stopped, effects stopped");
        LevelInfoLayer::onPlay(sender);
    }

    void onBack(CCObject *sender)
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
        fmod->stopAllEffects();
        gm->playMenuMusic();
        log::info("onBack triggered - stopping level music & play menu music");
        stopCheckMusicAndRetry();
        // remove delegate
        if (m_fields->m_musicDelegate)
        {
            MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate));
            delete static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
            m_fields->m_musicDelegate = nullptr;
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
            MusicDownloadManager::sharedState()->removeMusicDownloadDelegate(static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate));
            delete static_cast<SongDownloadForwarder *>(m_fields->m_musicDelegate);
            m_fields->m_musicDelegate = nullptr;
        }
    }
};

// playlayer stuff
class $modify(MyPlayLayer, PlayLayer)
{
    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        // Stop all music before initializing the play layer
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("PlayLayer init - ensuring music is stopped");
        fmod->stopAllMusic(true);

        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }
};