use crate::models::AudioDeviceInfo;

const VIRTUAL_AUDIO_KEYWORDS: &[&str] = &[
    "CABLE",
    "VB-Audio",
    "Virtual",
    "Voicemeeter",
    "VoiceMeeter",
    "VBVMAUX",
    "BlackHole",
];

pub fn contains_virtual_device(devices: &[AudioDeviceInfo]) -> bool {
    devices.iter().any(|device| {
        let name_lower = device.name.to_lowercase();
        VIRTUAL_AUDIO_KEYWORDS
            .iter()
            .any(|kw| name_lower.contains(&kw.to_lowercase()))
    })
}
