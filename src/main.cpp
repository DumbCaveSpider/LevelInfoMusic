#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

class MyLevelInfoLayer;

// Helper class to handle music download events
class LevelMusicDelegate : public MusicDownloadDelegate
{
public:
    MyLevelInfoLayer *m_levelInfoLayer = nullptr;
    int m_targetSongID = 0;

    LevelMusicDelegate(MyLevelInfoLayer *layer, int songID) : m_levelInfoLayer(layer), m_targetSongID(songID) {}

    virtual void downloadSongFinished(int songID) override
    {
        if (songID == m_targetSongID && m_levelInfoLayer)
        {
            log::info("Song download completed for level! Playing song ID: {}", songID);

            auto fmod = FMODAudioEngine::sharedEngine();
            auto musicManager = MusicDownloadManager::sharedState();

            // get the file path and play the song
            auto songPath = musicManager->pathForSong(songID);
            if (!songPath.empty())
            {
                fmod->playMusic(songPath, true, 1.0f, 1);
                log::debug("Auto-playing downloaded song: {}", songPath);
            }
        }
    }

    virtual void downloadSongFailed(int songID, GJSongError error) override
    {
        if (songID == m_targetSongID)
        {
            log::warn("Failed to download song {} for current level", songID);
        }
    }
};

class $modify(MyLevelInfoLayer, LevelInfoLayer)
{
    struct Fields
    {
        float m_originalVolume = 0.0f;
        int m_currentLevelSongID = 0;
        LevelMusicDelegate *m_musicDelegate = nullptr;
    };

    bool init(GJGameLevel *level, bool challenge)
    {
        if (!LevelInfoLayer::init(level, challenge))
            return false;

        // check if the music for this level is downloaded
        auto musicManager = MusicDownloadManager::sharedState();

        // get the fadetime from the settings
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");

        // store the current level's song ID for later use
        m_fields->m_currentLevelSongID = level->m_songID;

        // register as a download delegate to listen for download completion
        if (level->m_songID != 0)
        {
            m_fields->m_musicDelegate = new LevelMusicDelegate(this, level->m_songID);
            musicManager->addMusicDownloadDelegate(m_fields->m_musicDelegate);
        }

        if (level->m_songID != 0 && !musicManager->isSongDownloaded(level->m_songID))
        {
            log::warn("Level custom music not downloaded: {} - Starting download...", level->m_songID);
            // start downloading the song if it's not available
            musicManager->downloadSong(level->m_songID);
        }
        else
        {
            if (level->m_songID != 0)
            {
                log::debug("Level uses custom music ID: {}", level->m_songID);

                // get the FMOD audio engine and store original volume
                auto fmod = FMODAudioEngine::sharedEngine();
                m_fields->m_originalVolume = fmod->getBackgroundMusicVolume();

                // for custom songs, we need to get the file path and play it
                auto songPath = musicManager->pathForSong(level->m_songID);
                if (!songPath.empty())
                {

                    // fades in the custom song and fade out the background music
                    fmod->playMusic(songPath, true, fadeTime, 1);
                    log::debug("Playing custom song: {}", songPath);
                }
            }
            else
            {
                log::debug("Level uses built-in audio track: {}", level->m_audioTrack);
            }
        }

        return true;
    }

    void onBack(CCObject *sender)
    {
        auto fmod = FMODAudioEngine::sharedEngine();
        auto musicManager = MusicDownloadManager::sharedState();
        auto gm = GameManager::sharedState();

        // get the fadetime from the settings
        float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");

        // unregister and cleanup the delegate when leaving
        if (m_fields->m_musicDelegate)
        {
            musicManager->removeMusicDownloadDelegate(m_fields->m_musicDelegate);
            delete m_fields->m_musicDelegate;
            m_fields->m_musicDelegate = nullptr;
        }
        fmod->fadeInMusic(fadeTime, m_fields->m_originalVolume);
        gm->playMenuMusic();

        LevelInfoLayer::onBack(sender);
    }
};
