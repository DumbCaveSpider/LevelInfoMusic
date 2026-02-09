#include "Geode/cocos/cocoa/CCObject.h"
#include "Geode/loader/Log.hpp"
#include <Geode/Geode.hpp>
#include <thread>
#include <chrono>
#include <Geode/modify/CustomSongWidget.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

// i have to refactor the entire code because its horrible
// this time its more compact and actual readable
// trust me this is WAY WAY better and didnt took that long to do

unsigned int g_backgroundMusicPosition =
    0; // global variable to store background music position
bool g_customMusicPlayed =
    false; // global variable to check if custom music was actually played

namespace {
void storeMenuBGPosition() {
  FMOD::Channel *channel = nullptr;
  auto fmod = FMODAudioEngine::sharedEngine();
  auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
  if (result == FMOD_OK && channel) {
    channel->getPosition(&g_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
    log::info("store menu bg music position: {}", g_backgroundMusicPosition);
  }
}

void restoreMenuBGPosition() {
  FMOD::Channel *channel = nullptr;
  auto fmod = FMODAudioEngine::sharedEngine();
  auto resultBG = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
  if (resultBG == FMOD_OK && channel) {
    channel->setPosition(g_backgroundMusicPosition, FMOD_TIMEUNIT_MS);
    log::info("restore menu bg music position: {}", g_backgroundMusicPosition);
  }
}

void applyPositioning(FMODAudioEngine *fmod, FMOD::Channel *channel,
                      bool playMid, bool randomOffset) {
  if (!channel)
    return;
  if (playMid) {
    channel->setChannelGroup(fmod->m_backgroundMusicChannel);
    channel->setPosition(fmod->getMusicLengthMS(0) / 2, FMOD_TIMEUNIT_MS);
  }
  if (randomOffset) {
    unsigned int musicLength = fmod->getMusicLengthMS(0);
    if (musicLength > 0) {
      unsigned int randomPosition =
          geode::utils::random::generate<unsigned int>(0, musicLength - 1);
      channel->setPosition(randomPosition, FMOD_TIMEUNIT_MS);
      log::info("random position: {}", randomPosition);
    }
  }
}

// Try to play the music and apply positioning once the channel is active.
// Returns true if playback was confirmed and positioning applied.
bool playMusicAndApply(const std::string &path, float fadeTime, bool playMid, bool randomOffset) {
  auto fmod = FMODAudioEngine::sharedEngine();
  fmod->stopAllMusic(true);
  fmod->playMusic(path, true, fadeTime, 0);

  for (int i = 0; i < 10; ++i) {
    FMOD::Channel *channel = nullptr;
    auto result = fmod->m_backgroundMusicChannel->getChannel(0, &channel);
    if (result == FMOD_OK && channel) {
      bool isPlaying = false;
      channel->isPlaying(&isPlaying);
      if (isPlaying || fmod->getMusicLengthMS(0) > 0) {
        applyPositioning(fmod, channel, playMid, randomOffset);
        g_customMusicPlayed = true;
        log::debug("music started: {}", path);
        return true;
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  log::warn("failed to start music: {}", path);
  g_customMusicPlayed = false;
  return false;
}

void stopAndRestoreMenuMusicIfCustomPlayed() {
  if (!g_customMusicPlayed) {
    log::info("custom music was not played, not stopping music");
    return;
  }

  log::info("stop custom music when exiting level info");
  FMODAudioEngine::sharedEngine()->stopAllMusic(true);
  GameManager::sharedState()->playMenuMusic();
  restoreMenuBGPosition();
  g_customMusicPlayed = false;
}
} // namespace

class $modify(LevelInfoLayer) {
  void onEnterTransitionDidFinish() {
    LevelInfoLayer::onEnterTransitionDidFinish(); // call original function

    FMOD::Channel *channel = nullptr;
    auto fmod = FMODAudioEngine::sharedEngine();
    auto musicManager = MusicDownloadManager::sharedState();
    auto level = this->m_level;

    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
    bool playMid = Mod::get()->getSettingValue<bool>("playMid");
    bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");

    log::info("music used: {}", level->m_songID);
    log::info("music length: {}", fmod->getMusicLengthMS(0));
    // check if the level has custom music
    if (level && level->m_songID != 0) {
      auto songPath = musicManager->pathForSong(level->m_songID);

      // check if the music is not downloaded
      if (!musicManager->isSongDownloaded(level->m_songID)) {
        log::info("song not downloaded");
        g_customMusicPlayed = false;
        return;
      }

      if (songPath.empty()) {
        log::warn("no custom music found for id {}", level->m_songID);
        g_customMusicPlayed = false;
        return;
      }

      log::info("playing custom music: {}", songPath);

      if (!playMusicAndApply(songPath, fadeTime, playMid, randomOffset)) {
        g_customMusicPlayed = false;
        return;
      }
    }

    // uses default audio track if no custom music
    else if (level && level->m_audioTrack != -1) {
      auto trackPath = LevelTools::getAudioFileName(level->m_audioTrack);
      if (trackPath.empty()) {
        log::warn("no audio track found for id {}", level->m_audioTrack);
        g_customMusicPlayed = false;
        return;
      }
      log::info("playing default track: {}", trackPath);

      if (!playMusicAndApply(trackPath, fadeTime, playMid, randomOffset)) {
        g_customMusicPlayed = false;
        return;
      }
    }
  }

  // store the current background music position when entering level info
  bool init(GJGameLevel *level, bool challenge) {
    if (!LevelInfoLayer::init(level, challenge))
      return false;
    storeMenuBGPosition();

    return true;
  }

  void loadLevelStep() {
    auto fmod = FMODAudioEngine::sharedEngine();
    fmod->stopAllMusic(true);
    log::info("stop music when loading level");
    LevelInfoLayer::loadLevelStep();
  }

  // stop music if one of these are called
  void onBack(CCObject *sender) {
    this->stopCurrentMusic(this->m_level);
    LevelInfoLayer::onBack(sender);
  }

  // my wacky functions :D
  void stopCurrentMusic(GJGameLevel *level) {
    stopAndRestoreMenuMusicIfCustomPlayed();
  }
};

class $modify(CustomSongWidget) {
  void downloadSongFinished(int p0) {
    // auto play the custom music when download finishes

    auto songPath =
        MusicDownloadManager::sharedState()->pathForSong(this->m_customSongID);
    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
    bool playMid = Mod::get()->getSettingValue<bool>("playMid");
    bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");
    // play and apply positioning
    if (!playMusicAndApply(songPath, fadeTime, playMid, randomOffset)) {
      g_customMusicPlayed = false;
    }
    CustomSongWidget::downloadSongFinished(p0);
  }

  void deleteSong() {
    // play the menu music when deleting the custom song
    CustomSongWidget::deleteSong();
    FMODAudioEngine::sharedEngine()->stopAllMusic(true);
    GameManager::sharedState()->playMenuMusic();
    g_customMusicPlayed = false; // reset when deleting
  }
};

class $modify(EditLevelLayer) {
  // store the current background music position when entering level info
  bool init(GJGameLevel *level) {
    if (!EditLevelLayer::init(level))
      return false;

    log::debug("EditLevelLayer init, level: {}, playEditorLevel: {}", level->m_levelID, Mod::get()->getSettingValue<bool>("playEditorLevel"));

    // do nothing if editor-level playback is disabled
    if (!Mod::get()->getSettingValue<bool>("playEditorLevel"))
      return true;

    // if custom music was already played and is still playing, skip
    {
      FMOD::Channel *bgChannel = nullptr;
      auto fmodEngine = FMODAudioEngine::sharedEngine();
      auto resCh =
          fmodEngine->m_backgroundMusicChannel->getChannel(0, &bgChannel);
      if (g_customMusicPlayed && resCh == FMOD_OK && bgChannel) {
        bool isPlaying = false;
        log::debug("custom music was already played, checking if it's still playing");
        bgChannel->isPlaying(&isPlaying);
        if (isPlaying) {
          log::debug("custom music is already playing, skipping music playback in editor");
          return true;
        }
      }
    }

    FMOD::Channel *channel = nullptr;
    auto fmod = FMODAudioEngine::sharedEngine();
    auto musicManager = MusicDownloadManager::sharedState();

    float fadeTime = Mod::get()->getSettingValue<float>("fadeTime");
    bool playMid = Mod::get()->getSettingValue<bool>("playMid");
    bool randomOffset = Mod::get()->getSettingValue<bool>("randomOffset");

    log::info("music used: {}", level->m_songID);
    log::info("music length: {}", fmod->getMusicLengthMS(0));

    // check if the level has custom music
    if (level && level->m_songID != 0) {
      auto songPath = musicManager->pathForSong(level->m_songID);

      // check if the music is not downloaded
      if (!musicManager->isSongDownloaded(level->m_songID)) {
        log::info("song not downloaded");
        g_customMusicPlayed = false;
        return true;
      }

      if (songPath.empty()) {
        log::warn("no custom music found for id {}", level->m_songID);
        g_customMusicPlayed = false;
        return true;
      }

      log::info("playing custom music: {}", songPath);

      if (!playMusicAndApply(songPath, fadeTime, playMid, randomOffset)) {
        g_customMusicPlayed = false;
        return true;
      }
    }

    // uses default audio track if no custom music
    else if (level && level->m_audioTrack != -1) {
      auto trackPath = LevelTools::getAudioFileName(level->m_audioTrack);
      if (trackPath.empty()) {
        log::warn("no audio track found for id {}", level->m_audioTrack);
        g_customMusicPlayed = false;
        return true;
      }
      log::info("playing default track: {}", trackPath);

      if (!playMusicAndApply(trackPath, fadeTime, playMid, randomOffset)) {
        g_customMusicPlayed = false;
        return true;
      }
    }

    return true;
  }

  void onPlay(CCObject *sender) {
    this->stopCurrentMusic(this->m_level);
    g_customMusicPlayed = false; // reset since we're leaving the editor
    EditLevelLayer::onPlay(sender);
  }

  // stop music if one of these are called
  void onBack(CCObject *sender) {
    this->stopCurrentMusic(this->m_level);
    g_customMusicPlayed = false; // reset since we're leaving the editor
    EditLevelLayer::onBack(sender);
  }

  // my wacky functions :D
  void stopCurrentMusic(GJGameLevel *level) {
    stopAndRestoreMenuMusicIfCustomPlayed();
  }
};