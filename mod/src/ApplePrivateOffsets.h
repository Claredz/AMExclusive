#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

// Apple-private ABI inventory for Apple Music 1.6.4.90 / 1.1540.23042.0 and
// 1.7.0.155 / 1.1792.39682.0 x64. Each entry records the semantic
// role and the fingerprint that must accompany it. Public API constants live
// elsewhere; private RVAs, object fields, call-stack fingerprints and observed
// binary layouts are kept here so a package update has one review surface.
namespace ammod::apple_private {

namespace verified_build {
inline constexpr wchar_t kAppleMusicFileVersion[] = L"1.6.4.90";
inline constexpr wchar_t kPackageVersion[] = L"1.1540.23042.0";
inline constexpr wchar_t kArchitecture[] = L"x64";
} // namespace verified_build

namespace core_media_filter {
inline constexpr std::size_t kFilterClassTable = 0x18;
inline constexpr std::size_t kClassTableMethods = 0x10;
inline constexpr std::size_t kSimpleApplySlot = 0x18;
inline constexpr std::size_t kTreeApplySlot = 0x20;
inline constexpr std::size_t kSimplePredicate = 0x40;
inline constexpr std::size_t kTreeChildren = 0x30;
inline constexpr std::size_t kTreeFallback = 0x38;
inline constexpr std::size_t kStrictContext = 0x68;
// Constructor fingerprint for the verified AllowableMediaSubtypes simple filter.
// A different priority/callback shape means the private ABI has drifted.
inline constexpr std::uint32_t kStrictConstructorPriority = 0x375;
// Both builds have the same consumed filter layout and function prologues.
// RVAs are selected only by an exact PE timestamp/SizeOfImage pair, followed
// by both byte checks. A matching prologue alone never authorizes a new build.
struct BuildProfile {
    const wchar_t* appleMusicVersion;
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t simpleApplyRva;
    std::uintptr_t treeApplyRva;
};
inline constexpr std::array<BuildProfile, 2> kBuildProfiles{{
    {L"1.6.4.90", 0x69B9C1B1, 0x01498000, 0x006CA8E0, 0x006E2F10},
    {L"1.7.0.155", 0x6AC9C301, 0x016B7000, 0x007DC3A0, 0x007F4A60},
}};

constexpr const BuildProfile* FindVerifiedBuild(std::uint32_t timestamp,
                                               std::uint32_t imageSize) noexcept {
    for (const auto& build : kBuildProfiles) {
        if (build.timestamp == timestamp && build.imageSize == imageSize) return &build;
    }
    return nullptr;
}

inline constexpr std::size_t kPrologueBytes = 32;
inline constexpr std::array<std::uint8_t, kPrologueBytes> kSimpleApplyPrologue{
    0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x8D,
    0x68, 0xA1, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00,
    0x00, 0x48, 0x89, 0x58, 0x18, 0x0F, 0x57, 0xC0,
    0x4C, 0x89, 0x68, 0xD8, 0x48, 0x8B, 0xD9, 0x4C};
inline constexpr std::array<std::uint8_t, kPrologueBytes> kTreeApplyPrologue{
    0x4C, 0x89, 0x4C, 0x24, 0x20, 0x4C, 0x89, 0x44,
    0x24, 0x18, 0x48, 0x89, 0x54, 0x24, 0x10, 0x48,
    0x89, 0x4C, 0x24, 0x08, 0x55, 0x53, 0x56, 0x57,
    0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57};

inline const BuildProfile* MatchVerifiedImage(
    std::uint32_t timestamp, std::span<const std::uint8_t> image) noexcept {
    if (image.size() > UINT32_MAX) return nullptr;
    const auto* build = FindVerifiedBuild(timestamp, static_cast<std::uint32_t>(image.size()));
    if (!build) return nullptr;
    const auto matches = [&](std::uintptr_t rva, const auto& bytes) {
        return rva <= image.size() && bytes.size() <= image.size() - rva &&
               std::memcmp(image.data() + rva, bytes.data(), bytes.size()) == 0;
    };
    return matches(build->simpleApplyRva, kSimpleApplyPrologue) &&
           matches(build->treeApplyRva, kTreeApplyPrologue) ? build : nullptr;
}
} // namespace core_media_filter

namespace agent_stack {
// Fingerprint for the AMPLibraryAgent.exe local-file seek path calling
// AudioConverterReset (the hook DLL's host image, resolved through
// GetModuleHandleW(nullptr)). These are image-relative return-address RVAs,
// not absolute process addresses. The third frame has two observed compiler
// paths. Each RVA sits directly after an E8 CALL in the verified build.
inline constexpr std::size_t kLocalSeekResetFrames = 3;
struct BuildProfile {
    std::uint32_t timestamp;
    std::uint32_t imageSize;
    std::uintptr_t frame0, frame1;
    std::array<std::uintptr_t, 2> frame2;
};
inline constexpr std::array<BuildProfile, 2> kBuildProfiles{{
    {0x69C4EC34, 0x01262000, 0x0040B9FA, 0x0082F6BC, {0x00B1E8AD, 0x00B1E972}},
    {0x6AD9AC61, 0x01296000, 0x00435FE6, 0x008669AB, {0x00B5BCA1, 0x00B5BD64}},
}};

constexpr bool IsLocalSeekReset(std::uint32_t timestamp, std::uint32_t imageSize,
                                const std::array<std::uintptr_t, 3>& frames) noexcept {
    for (const auto& build : kBuildProfiles) {
        if (build.timestamp == timestamp && build.imageSize == imageSize &&
            frames[0] == build.frame0 && frames[1] == build.frame1 &&
            (frames[2] == build.frame2[0] || frames[2] == build.frame2[1])) return true;
    }
    return false;
}
} // namespace agent_stack

namespace audio_converter_property {
inline constexpr std::uint32_t kCursor = 0x63706563; // 'cpec'
inline constexpr std::uint32_t kCurrentInputDescription = 0x61636964; // 'acid'
inline constexpr std::uint32_t kCurrentOutputDescription = 0x61636F64; // 'acod'
} // namespace audio_converter_property

namespace alac_cookie {
// ALACSpecificConfig can appear bare, after an atom header, or after two atom
// headers. Fingerprint: supported bit depth/channels/rate sanity checks at use.
inline constexpr std::array<std::uint32_t, 3> kCandidateOffsets{0, 12, 24};
inline constexpr std::uint32_t kConfigBytes = 24;
inline constexpr std::uint32_t kBitDepth = 5;
inline constexpr std::uint32_t kChannels = 9;
inline constexpr std::uint32_t kSampleRate = 20;
} // namespace alac_cookie

} // namespace ammod::apple_private
