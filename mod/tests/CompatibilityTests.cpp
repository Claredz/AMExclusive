#include <windows.h>
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include "ApplePlayback.h"
#include "ApplePrivateOffsets.h"
#ifdef AMEX_CHECK_APPLE_PROJECTION
#ifdef GetCurrentTime
#undef GetCurrentTime
#endif
#include <winrt/AMP.Services.h>
#endif

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace playback = ammod::apple_private::playback;
namespace filters = ammod::apple_private::core_media_filter;

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

template<typename Interface>
struct InspectableMock : Interface {
    std::atomic<ULONG> references{1};
    HRESULT __stdcall QueryInterface(REFIID iid, void** value) noexcept override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != __uuidof(Interface) && iid != __uuidof(::IInspectable) &&
            iid != __uuidof(::IUnknown)) return E_NOINTERFACE;
        *value = static_cast<Interface*>(this);
        AddRef();
        return S_OK;
    }
    ULONG __stdcall AddRef() noexcept override { return ++references; }
    ULONG __stdcall Release() noexcept override {
        const auto remaining = --references;
        if (!remaining) delete this;
        return remaining;
    }
    HRESULT __stdcall GetIids(ULONG* count, IID** values) noexcept override {
        *count = 0; *values = nullptr; return S_OK;
    }
    HRESULT __stdcall GetRuntimeClassName(HSTRING* value) noexcept override {
        *value = nullptr; return S_OK;
    }
    HRESULT __stdcall GetTrustLevel(TrustLevel* value) noexcept override {
        *value = BaseTrust; return S_OK;
    }
    virtual ~InspectableMock() = default;
};

#define UNUSED_GETTER(name, type) \
    HRESULT __stdcall name(type*) noexcept override { return E_NOTIMPL; }

struct StateMock final : InspectableMock<playback::abi::IPlayerState> {
    UNUSED_GETTER(get_CanPlay, bool)
    UNUSED_GETTER(get_CanPause, bool)
    UNUSED_GETTER(get_CanStop, bool)
    UNUSED_GETTER(get_CanRewind, bool)
    UNUSED_GETTER(get_CanFF, bool)
    UNUSED_GETTER(get_CanGoBack, bool)
    UNUSED_GETTER(get_CanSkipPrevious, bool)
    UNUSED_GETTER(get_CanSkipNext, bool)
    UNUSED_GETTER(get_PreferMusicControls, bool)
    UNUSED_GETTER(get_CanShuffle, bool)
    UNUSED_GETTER(get_ShuffleMode, std::int32_t)
    UNUSED_GETTER(get_CanRepeat, bool)
    UNUSED_GETTER(get_RepeatMode, std::int32_t)
    UNUSED_GETTER(get_TargetPlayRate, double)
    UNUSED_GETTER(get_CanSetVolume, bool)
    UNUSED_GETTER(get_Volume, double)
    UNUSED_GETTER(get_CanMute, bool)
    UNUSED_GETTER(get_IsMuted, bool)
    HRESULT __stdcall get_IsPaused(bool* value) noexcept override { *value = false; return S_OK; }
    HRESULT __stdcall get_IsPlaying(bool* value) noexcept override { *value = true; return S_OK; }
};

template<typename Interface>
struct ItemMock final : InspectableMock<Interface> {
    UNUSED_GETTER(get_Subtitle, void*)
    UNUSED_GETTER(get_Artist, void*)
    UNUSED_GETTER(get_Album, void*)
    UNUSED_GETTER(get_TrackNumber, std::int32_t)
    UNUSED_GETTER(get_TrackCount, std::int32_t)
    UNUSED_GETTER(get_Artwork, void*)
    UNUSED_GETTER(get_PlaybackMediaType, std::int32_t)
    UNUSED_GETTER(get_ExplicitState, std::int32_t)
    UNUSED_GETTER(get_VideoRatingString, void*)
    UNUSED_GETTER(get_RottonTomatoRating, void*)
    UNUSED_GETTER(get_RottonTomatoScore, void*)
    UNUSED_GETTER(get_CommonSenseRating, void*)
    UNUSED_GETTER(get_ISOReleaseDate, void*)
    UNUSED_GETTER(get_Name, void*)
    UNUSED_GETTER(get_TVShowName, void*)
    UNUSED_GETTER(get_Genre, void*)
    UNUSED_GETTER(get_Description, void*)
    UNUSED_GETTER(get_Accessibility, void*)
    UNUSED_GETTER(get_IsPlaceholder, bool)
    UNUSED_GETTER(get_IsRadio, bool)
    UNUSED_GETTER(get_IsRadioLiveStream, bool)
    UNUSED_GETTER(get_Reactions, void*)
    UNUSED_GETTER(get_ShowCanonicalId, void*)
    UNUSED_GETTER(get_ScrubberMode, std::int32_t)
    UNUSED_GETTER(get_CurrentPosition, std::int64_t)
    UNUSED_GETTER(get_Duration, std::int64_t)
    UNUSED_GETTER(get_LoadTimeIsValid, bool)
    UNUSED_GETTER(get_LoadTime, std::int64_t)
    UNUSED_GETTER(get_MinLoadTime, std::int64_t)
    UNUSED_GETTER(get_CanBePositioned, bool)
    UNUSED_GETTER(get_LiveMode, std::int32_t)
    UNUSED_GETTER(get_CurrentDate, std::uint64_t)
    UNUSED_GETTER(get_AudioFormat, std::int32_t)
    UNUSED_GETTER(get_AudioFormatSampleRate, void*)
    UNUSED_GETTER(get_AudioFormatBitDepth, void*)
    UNUSED_GETTER(get_HLSMetadata, void*)
    UNUSED_GETTER(get_HasVideo, bool)
    UNUSED_GETTER(get_VideoPresentationSize, winrt::Windows::Foundation::Size)
    UNUSED_GETTER(get_VideoColorCapability, std::int32_t)
    HRESULT __stdcall get_PlayQueueItemID(std::uint64_t* value) noexcept override {
        *value = 0x123456789ABCDEF0ull; return S_OK;
    }
};
#undef UNUSED_GETTER

template<typename PlayerInterface, typename ItemInterface>
struct PlayerMock final : InspectableMock<PlayerInterface> {
    winrt::com_ptr<::IInspectable> item, state;
    PlayerMock() {
        item.attach(new ItemMock<ItemInterface>);
        state.attach(new StateMock);
    }
    HRESULT __stdcall get_NowPlayingItem(void** value) noexcept override {
        if (!value) return E_POINTER;
        *value = item.get(); item->AddRef(); return S_OK;
    }
    HRESULT __stdcall get_PlayerState(void** value) noexcept override {
        if (!value) return E_POINTER;
        *value = state.get(); state->AddRef(); return S_OK;
    }
};

template<typename PlayerInterface, typename ItemInterface>
void TestPlaybackAbi() {
    playback::Player player{static_cast<PlayerInterface*>(
        new PlayerMock<PlayerInterface, ItemInterface>), winrt::take_ownership_from_abi};
    Require(player.NowPlayingItem().PlayQueueItemID() == 0x123456789ABCDEF0ull,
            "queue identity must be read through the correct interface");
    Require(player.PlayerState().IsPlaying() && !player.PlayerState().IsPaused(),
            "canonical playback state must remain available");
}

void TestUnknownAbi() {
    playback::Player player{static_cast<::IInspectable*>(new InspectableMock<::IInspectable>),
                           winrt::take_ownership_from_abi};
    bool rejected = false;
    try { (void)player.PlayerState(); }
    catch (const winrt::hresult_error& e) { rejected = e.code() == E_NOINTERFACE; }
    Require(rejected, "unknown player ABI must fail without guessing a vtable");
}

void TestBuildGuards() {
    for (const auto& build : filters::kBuildProfiles) {
        std::vector<std::uint8_t> image(build.imageSize);
        std::copy(filters::kSimpleApplyPrologue.begin(), filters::kSimpleApplyPrologue.end(),
                  image.begin() + build.simpleApplyRva);
        std::copy(filters::kTreeApplyPrologue.begin(), filters::kTreeApplyPrologue.end(),
                  image.begin() + build.treeApplyRva);
        Require(filters::MatchVerifiedImage(build.timestamp, image) == &build, "known build rejected");
        Require(!filters::MatchVerifiedImage(build.timestamp ^ 1, image), "unknown timestamp accepted");
        image[build.simpleApplyRva] ^= 1;
        Require(!filters::MatchVerifiedImage(build.timestamp, image), "altered simple function accepted");
        image[build.simpleApplyRva] ^= 1;
        image[build.treeApplyRva + 20] ^= 1;
        Require(!filters::MatchVerifiedImage(build.timestamp, image), "altered tree function accepted");
        image.resize(128);
        Require(!filters::MatchVerifiedImage(build.timestamp, image), "truncated image accepted");
    }
}

void TestRealCoreMedia(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    Require(stream.good(), "cannot read CoreMedia fixture");
    const auto size = static_cast<std::size_t>(stream.tellg());
    std::vector<std::uint8_t> file(size);
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(file.data()), static_cast<std::streamsize>(size));
    Require(size >= sizeof(IMAGE_DOS_HEADER), "short PE");
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    Require(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
            static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) <= size, "invalid PE header");
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(file.data() + dos->e_lfanew);
    Require(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.SizeOfImage < 64 * 1024 * 1024,
            "invalid PE image");
    std::vector<std::uint8_t> image(nt->OptionalHeader.SizeOfImage);
    auto* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        Require(section[i].PointerToRawData <= file.size() &&
                section[i].SizeOfRawData <= file.size() - section[i].PointerToRawData &&
                section[i].VirtualAddress <= image.size() &&
                section[i].SizeOfRawData <= image.size() - section[i].VirtualAddress, "invalid PE section");
        std::copy_n(file.data() + section[i].PointerToRawData, section[i].SizeOfRawData,
                    image.data() + section[i].VirtualAddress);
    }
    const auto* build = filters::MatchVerifiedImage(nt->FileHeader.TimeDateStamp, image);
    Require(build != nullptr, "actual CoreMedia binary does not match its compatibility profile");
    std::wcout << L"Verified CoreMedia for Apple Music " << build->appleMusicVersion << L'\n';
}

int wmain(int argc, wchar_t** argv) {
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        TestPlaybackAbi<playback::abi::IPlayer, playback::abi::INowPlayingItem>();
        TestPlaybackAbi<playback::abi::IPlayer17, playback::abi::INowPlayingItem17>();
        TestUnknownAbi();
        TestBuildGuards();
#ifdef AMEX_CHECK_APPLE_PROJECTION
        // A normal local build generates these types from the installed Apple
        // metadata. Only compare the two supported current/legacy GUID pairs.
        const auto playerIid = winrt::guid_of<winrt::AMP::Services::IPlayer>();
        const auto itemIid = winrt::guid_of<winrt::AMP::Services::CommonModels::INowPlayingItem>();
        Require((playerIid == winrt::guid{__uuidof(playback::abi::IPlayer)} &&
                 itemIid == winrt::guid{__uuidof(playback::abi::INowPlayingItem)}) ||
                (playerIid == winrt::guid{__uuidof(playback::abi::IPlayer17)} &&
                 itemIid == winrt::guid{__uuidof(playback::abi::INowPlayingItem17)}),
                "installed Apple metadata is not a recorded playback ABI");
#endif
        for (int i = 1; i < argc; ++i) TestRealCoreMedia(argv[i]);
        std::cout << "Compatibility tests passed\n";
        return 0;
    } catch (const winrt::hresult_error& e) {
        std::cerr << "COM failure: " << std::hex << static_cast<unsigned long>(e.code().value) << '\n';
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
    }
    return 1;
}
