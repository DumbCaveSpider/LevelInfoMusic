#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

class $modify(MyLevelInfoLayer, LevelInfoLayer)
{
    struct Fields
    {
        float m_originalVolume = 0.0f;
        int m_currentLevelSongID = 0;
        void *m_musicDelegate = nullptr;
        bool m_hasInitialized = false;
        GJGameLevel *m_currentLevel = nullptr;
        int m_retryCount = 0;
        static const int MAX_RETRIES = 5;
    };

public:
    std::string getBuiltInAudioPath(int audioTrack)
    {
        auto gameDir = geode::dirs::getResourcesDir();

        switch (audioTrack)
        {
        case 0:
            return geode::utils::string::pathToString(gameDir / "StereoMadness.mp3");
        case 1:
            return geode::utils::string::pathToString(gameDir / "BackOnTrack.mp3");
        case 2:
            return geode::utils::string::pathToString(gameDir / "Polargeist.mp3");
        case 3:
            return geode::utils::string::pathToString(gameDir / "DryOut.mp3");
        case 4:
            return geode::utils::string::pathToString(gameDir / "BaseAfterBase.mp3");
        case 5:
            return geode::utils::string::pathToString(gameDir / "CantLetGo.mp3");
        case 6:
            return geode::utils::string::pathToString(gameDir / "Jumper.mp3");
        case 7:
            return geode::utils::string::pathToString(gameDir / "TimeMachine.mp3");
        case 8:
            return geode::utils::string::pathToString(gameDir / "Cycles.mp3");
        case 9:
            return geode::utils::string::pathToString(gameDir / "xStep.mp3");
        case 10:
            return geode::utils::string::pathToString(gameDir / "Clutterfunk.mp3");
        case 11:
            return geode::utils::string::pathToString(gameDir / "TheoryOfEverything.mp3");
        case 12:
            return geode::utils::string::pathToString(gameDir / "Electroman.mp3");
        case 13:
            return geode::utils::string::pathToString(gameDir / "Clubstep.mp3");
        case 14:
            return geode::utils::string::pathToString(gameDir / "Electrodynamix.mp3");
        case 15:
            return geode::utils::string::pathToString(gameDir / "HexagonForce.mp3");
        case 16:
            return geode::utils::string::pathToString(gameDir / "BlastProcessing.mp3");
        case 17:
            // @geode-ignore(unknown-resource)
            return geode::utils::string::pathToString(gameDir / "TheoryOfEverything2.mp3");
        case 18:
            return geode::utils::string::pathToString(gameDir / "GeometricalDominator.mp3");
        case 19:
            return geode::utils::string::pathToString(gameDir / "Deadlocked.mp3");
        case 20:
            return geode::utils::string::pathToString(gameDir / "Fingerdash.mp3");
        case 21:
            return geode::utils::string::pathToString(gameDir / "Dash.mp3");
        default:
            return "";
        }
    }

    // the way it breaks if u exit a level so this function exist just to fix it
    void playCustomSong(const std::string &songPath, float fadeTime, bool playMid)
    {
        auto fmod = FMODAudioEngine::sharedEngine();

        if (!playMid)
        {
            // stop all music
            fmod->stopAllMusic(true);

            // play music at start
            fmod->playMusic(songPath, true, fadeTime, 1);
            log::info("Playing custom song from start: {}", songPath);
        }
        else
        {
            // stop all music
            fmod->stopAllMusic(true);

            // play music for the mid thing
            fmod->playMusic(songPath, true, fadeTime, 1);

            // Set position to middle using a delayed approach
            Loader::get()->queueInMainThread([this, songPath]()
                                             {
                auto audioEngine = FMODAudioEngine::sharedEngine();
                auto channelGroup = audioEngine->m_backgroundMusicChannel;
                if (channelGroup != nullptr) {
                    unsigned int lengthMs = audioEngine->getMusicLengthMS(1);
                    if (audioEngine && audioEngine->m_backgroundMusicChannel) {
                        log::info("Using fallback music length for positioning");
                    } else {
                        log::warn("Background music channel not available, using fallback");
                    }
                    
                    unsigned int middleMs = lengthMs / 2;
                    log::info("Setting music position to middle: {} ms (estimated)", middleMs);
                    
                    // Get the first channel from the channel group
                    FMOD::Channel* channel = nullptr;
                    auto result = channelGroup->getChannel(0, &channel);
                    if (result == FMOD_OK && channel) {
                        // Use position unit 1 for cross-platform compatibility (typically milliseconds)
                        auto setResult = channel->setPosition(middleMs, 1);
                        log::info("Channel position set result: {}", (int)setResult);
                    } else {
                        log::warn("Failed to get channel from group, result: {}", (int)result);
                    }
                } });
            log::info("Playing custom song from middle: {}", songPath);
        }
    }

    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        // Store the level reference for later use
        m_fields->m_currentLevel = level;
        m_fields->m_hasInitialized = true;

        // Initialize music on first load
        initializeLevelMusic();

        return true;
    }

    void initializeLevelMusic()
    {
        auto level = m_fields->m_currentLevel;
        if (!level)
            return;

        // check if the music for this level is downloaded
        auto musicManager = MusicDownloadManager::sharedState();

        // get da settings value
        // @geode-ignore(unknown-setting)
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
        bool playMid = Mod::get()->getSettingValue<bool>("playMid");

        // store the current level's song ID for later use
        m_fields->m_currentLevelSongID = level->m_songID;

        // get the FMOD audio engine and store original volume
        auto fmod = FMODAudioEngine::sharedEngine();
        m_fields->m_originalVolume = fmod->getBackgroundMusicVolume();

        // FORCEFULLY play music regardless of conditions
        if (level->m_songID != 0)
        {
            log::info("level custom music playback for song ID: {}", level->m_songID);

            // Try to play the song immediately if downloaded, otherwise force download
            if (musicManager->isSongDownloaded(level->m_songID))
            {
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {
                    log::info("Custom song is available, playing immediately: {}", songPath);
                    // stop all music
                    fmod->stopAllMusic(true);
                    playCustomSong(songPath, fadeTime, playMid);
                    // Schedule a retry check to ensure music is actually playing
                    scheduleRetryCheck();
                }
                else
                {
                    log::warn("Song reported as downloaded but path is empty, forcing download: {}", level->m_songID);
                    musicManager->downloadSong(level->m_songID);
                    // Schedule check to try again once download completes
                    scheduleDownloadCheck();
                }
            }
            else
            {
                log::info("Custom song not downloaded, forcing download: {}", level->m_songID);
                musicManager->downloadSong(level->m_songID);

                // Schedule check to try again once download completes
                scheduleDownloadCheck();
            }
        }
        else
        {
            // Built-in audio track - use helper function for cleaner code
            fmod->stopAllMusic(true);
            auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
            if (!audioPath.empty())
            {
                log::info("Level uses built-in audio track: {}, from path: {}", level->m_audioTrack, audioPath);
                fmod->playMusic(audioPath, true, fadeTime, level->m_audioTrack);
            }
            else
            {
                log::debug("Unknown built-in track ID: {}", level->m_audioTrack);
            }
        }
    }

    void scheduleDownloadCheck()
    {
        // Check every 2 seconds for download completion
        Loader::get()->queueInMainThread([this]()
                                         {
            auto level = m_fields->m_currentLevel;
            if (!level) return;

            auto musicManager = MusicDownloadManager::sharedState();
            if (level->m_songID != 0 && musicManager->isSongDownloaded(level->m_songID))
            {
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {
                    // @geode-ignore(unknown-setting)
                    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                    bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                    log::info("Download completed, playing custom song: {}", songPath);

                    auto fmod = FMODAudioEngine::sharedEngine();
                    // stop all music
                    fmod->stopAllMusic(true);
                    playCustomSong(songPath, fadeTime, playMid);
                }
            }
            else
            {
                // Keep checking until download completes
                scheduleDownloadCheck();
            } });
    }

    void scheduleRetryCheck()
    {
        // Reset retry count
        m_fields->m_retryCount = 0;

        // Schedule a check to ensure music is playing
        Loader::get()->queueInMainThread([this]()
                                         { this->checkMusicAndRetry(); });
    }

    void checkMusicAndRetry()
    {
        auto fmod = FMODAudioEngine::sharedEngine();

        // Check if music is actually playing
        if (m_fields->m_retryCount < Fields::MAX_RETRIES)
        {
            m_fields->m_retryCount++;
            log::warn("retry {}: Music not playing, attempting to force play again", m_fields->m_retryCount);

            // Force retry music initialization
            auto level = m_fields->m_currentLevel;
            if (level && level->m_songID != 0)
            {
                auto musicManager = MusicDownloadManager::sharedState();
                if (musicManager->isSongDownloaded(level->m_songID))
                {
                    auto songPath = musicManager->pathForSong(level->m_songID);
                    if (!songPath.empty())
                    {
                        // @geode-ignore(unknown-setting)
                        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                        bool playMid = Mod::get()->getSettingValue<bool>("playMid");
                        fmod->stopAllMusic(true);
                        playCustomSong(songPath, fadeTime, playMid);
                        log::info("Re-attempting custom song playback");
                    }
                }
            }
            else if (level)
            {
                // Retry built-in track using proper path resolution
                // @geode-ignore(unknown-setting)
                float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
                fmod->stopAllMusic(true);

                // Use helper function to get the correct path for built-in tracks
                auto audioPath = LevelTools::getAudioFileName(level->m_audioTrack);
                if (!audioPath.empty())
                {
                    fmod->playMusic(audioPath, true, fadeTime, 0);
                    log::info("Re-attempting built-in track playback: {}", audioPath);
                }
                else
                {
                    // Fallback to original method if path not found
                    fmod->playMusic("", true, fadeTime, level->m_audioTrack);
                    log::info("Re-attempting built-in track playback (fallback)");
                }
            }

            // Schedule another check if we haven't exceeded max retries
            if (m_fields->m_retryCount < Fields::MAX_RETRIES)
            {
                Loader::get()->queueInMainThread([this]()
                                                 { this->checkMusicAndRetry(); });
            }
        }
        else
        {
            log::error("Failed to play music after {} retries", Fields::MAX_RETRIES);
        }
    }

    void onBack(CCObject *sender)
    {
        auto fmod = FMODAudioEngine::sharedEngine();
        auto gm = GameManager::sharedState();

        // get the fadetime from the settings
        // @geode-ignore(unknown-setting)
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");

        // Stop the current level music forcefully
        fmod->stopAllMusic(true);
        log::info("Leaving LevelInfoLayer, level music stopped");

        // Fade back to menu music
        fmod->fadeInMusic(fadeTime, m_fields->m_originalVolume);
        gm->playMenuMusic();

        LevelInfoLayer::onBack(sender);
    }

    void onPlay(CCObject *sender)
    {
        // Stop the current level music and ensure it stays stopped
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("onPlay triggered - stopping level music");

        // Stop all music forcefully multiple times to ensure it's stopped
        fmod->stopAllMusic(true);
        fmod->stopAllMusic(false);

        // Also stop any effects that might be playing
        fmod->stopAllEffects();

        log::info("Music stopped, effects stopped, and volume set to 0 for level play");

        LevelInfoLayer::onPlay(sender);
    }
};

class $modify(MyPlayLayer, PlayLayer)
{
public:
    bool init(GJGameLevel *level, bool useReplay, bool dontCreateObjects)
    {
        // Stop all music before initializing the play layer
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("PlayLayer init - ensuring music is stopped");
        fmod->stopAllMusic(true);

        return PlayLayer::init(level, useReplay, dontCreateObjects);
    }

    void onQuit()
    {
        // Restore music when quitting the level
        auto fmod = FMODAudioEngine::sharedEngine();
        log::info("PlayLayer quit - restoring music volume");

        PlayLayer::onQuit();
    }
};
