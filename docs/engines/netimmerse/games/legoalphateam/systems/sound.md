# LEGO Alpha Team: sound

- Done: sound, through the A3D stand-in (`src/common/x86/a3d.cpp`,
  docs/common/x86.md). `NiA3dSoundSystem::Startup` now succeeds, and
  `DDStreamingAudioManager::AddSource` returns sources. The streaming
  thread fills them as their play positions move, and the engine hears
  them through its mixer.
  - Which sounds are which, as the game makes them (`NiA3dSource`'s
    type): the streamed ones (`DDStreamingAudioManager`: music such as
    `ThemeLoop.dds`, `OgelIntro.dds`, `RewardMusic.dds`, and the voices)
    are type 5, native, not placed. The interface's sounds
    (`DDUIManager::AddSound`) and the actors' skills (`DDSkill::Update`;
    a byte in the skill's data picks native or 3D) are loaded from files.
    The 3D ones are placed at their actors, in world units, with minimum
    and maximum distances of 200 to 300 and 600 to 10,000; the listener
    follows the camera (`NiA3dListener::A3dUpdate`, each frame from
    `NiA3dSoundSystem::Click`), right-handed with z up.
  - Two faults kept all but the streamed sounds from being heard (both in
    x86.md): `GetWaveFormat` answering success for an empty source, so
    `NiA3dSource::Load` never loaded the file, and the listener offered
    under the wrong IID, so it stayed at the origin. With both fixed,
    Mission One's skills and interface sounds load and play, placed
    around the camera.
