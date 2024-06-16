#include <array>

#include "Milan/Misc.hpp"
#include "Milan/NetProfStream.hpp"
#include "Proxies/Engine.hpp"
#include "Systems/Output.hpp"


// ! NetworkCapture implementation.
// ========================================

NetworkCapture* NetworkCapture::GlobalInstance = nullptr;
std::mutex NetworkCapture::GlobalMutex;

NetworkCapture::NetworkCapture()
    : StringTable{}, StringLookup{}, StringTierOffset{}
{
    MILAN_INFOW(L"netprof starting capture");

    SYSTEMTIME Time{};
    GetLocalTime(&Time);

    int const Instance = ::GetGameInstanceOrdinal();
    std::array<char, 256> FileNameBuffer{ '\0' };

    snprintf(FileNameBuffer.data(), FileNameBuffer.size(), "netprof-%04d%02d%02d-%02d%02d%02d-%d.mnpsdb",
        Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, Instance);
    auto const* FileName = FileNameBuffer.data();

    MILAN_TRACEW(L"netprof database path = %S", FileName);
    Stream = new BufferedFileOutputStream(FileName);

    StringTable.reserve(500);
    StringLookup.reserve(500);

    // Serialize header.

    std::span<BYTE> const Magic{ (BYTE*)k_serialMagic, k_serialMagicSize };
    std::array<BYTE, 52> const ReservedSpace{ 0u };

    Stream->WriteExact(Magic);
    Stream->WriteNumeric<LITTLE, INT>(k_serialVersion);
    Stream->WriteExact(ReservedSpace);

    MILAN_CHECKW(Magic.size() + sizeof(k_serialVersion) + ReservedSpace.size() == 64, L"");
    MILAN_CHECKW(Stream->Position() == 64, L"");
}

NetworkCapture::~NetworkCapture()
{
    MILAN_INFOW(L"netprof terminating capture");
    FlushRecords();
    delete Stream;
}

void NetworkCapture::FlushRecords()
{
    std::lock_guard<std::mutex> const _(Mutex);
    RecordStringTable_NoLock();
    Stream->Flush();
}

void NetworkCapture::CaptureFrameStart(float const Delta)
{
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::FrameStart;
    *this << Delta;
}

void NetworkCapture::CaptureLevelStart(FURL* const Url)
{
    std::lock_guard<std::mutex> const _(Mutex);

    *this << NPRecord::LevelStart;
    *this << Url->Protocol << Url->Host << Url->Map << Url->Portal << Url->Port;

    *this << static_cast<int>(Url->Op.Count());
    for (auto const& Op : Url->Op)
        *this << Op;
}

void NetworkCapture::CaptureRemoteFunction(UNetConnection* const Connection, AActor* const Actor, UFunction* const Function, INT const Bits)
{
    MILAN_CHECKW(Actor != nullptr, L"");
    MILAN_CHECKW(Function != nullptr, L"");
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::RemoteFunction;
    *this << Connection << Actor << Function << Bits;
}

void NetworkCapture::CaptureActorReplication(UNetConnection* const Connection, AActor* const Actor)
{
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::ActorReplication;
    *this << Connection << Actor;
}

void NetworkCapture::CapturePropertyReplication(UNetConnection* const Connection, UProperty* const Property, INT const Bits)
{
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::PropertyReplication;
    *this << Connection << Property << Bits;
}

void NetworkCapture::CaptureControlMessage(UNetConnection* const Connection, bool const bPendingLevel, ENetMsg const Type, FString const& Params)
{
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::ControlMessage;

    *this << (Connection != nullptr);
    if (Connection) *this << Connection;

    *this << bPendingLevel << (unsigned char)Type << Params;
}

void NetworkCapture::CaptureSessionEvent(UNetConnection* const Connection, FString const Event, FString const Description)
{
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::SessionEvent;
    *this << Connection << Event << Description;
}

void NetworkCapture::CaptureSocketCall(SOCKADDR_IN* const Remote, bool const bSending, INT const Bits)
{
    std::lock_guard<std::mutex> const _(Mutex);
    *this << NPRecord::SocketCall;
    *this << Remote << bSending << Bits;
}

void NetworkCapture::CaptureSendBunch(UChannel* const Channel, INT const Bits)
{
    auto const Proxy = reinterpret_cast<UChannel_Proxy*>(Channel);
    std::lock_guard<std::mutex> const _(Mutex);

    *this << NPRecord::SendBunch;
    *this << Proxy->Connection << Channel << Bits;
}

void NetworkCapture::RecordStringTable_NoLock()
{
    MILAN_DEBUGW(L"netprof: writing tiered string table, offset = %llu, size = %llu",
        StringTierOffset, StringTable.size());

    if (StringTierOffset >= StringTable.size())
    {
        // Let's avoid empty records.
        return;
    }

    int const Count = static_cast<int>(StringTable.size() - StringTierOffset) - 1;

    *this << NPRecord::TieredStringLookup;
    *this << Count;

    size_t i;
    for (i = StringTierOffset; i < StringTable.size(); ++i)
    {
        auto const Length = StringTable[i].length();
        MILAN_CHECKW(Length < UINT16_MAX, L"");
        *this << static_cast<short>(Length);

        if (Length != 0)
        {
            auto const Data = reinterpret_cast<unsigned char*>(StringTable[i].data());
            Stream->WriteExact(std::span{ Data, Length * 2 });
        }
    }

    StringTierOffset = StringTable.size();

    MILAN_TRACEW(L"netprof: last serialized index = %d, new offset = %llu", i, StringTable.size());
}

NetworkCapture& NetworkCapture::operator<<(NPRecord const Type)
{
    MILAN_CHECKW(Type < NPRecord::MAX, L"");
    auto const Value = static_cast<unsigned char>(Type);
    Stream->WriteNumeric<LITTLE, BYTE>(Value | 0x80);
    return *this;
}

NetworkCapture& NetworkCapture::operator<<(bool const Value)
{
    Stream->WriteNumeric<LITTLE, BYTE>(Value ? 1u : 0u);
    return *this;
}

NetworkCapture& NetworkCapture::operator<<(short const Value)
{
    return *this << static_cast<long long>(Value);
}

NetworkCapture& NetworkCapture::operator<<(int const Value)
{
    return *this << static_cast<long long>(Value);
}

NetworkCapture& NetworkCapture::operator<<(long long const Value)
{
    //Stream->WriteNumeric<LITTLE>(Value);
    //return *this;

    MILAN_CHECKW(Value >= 0, L"");
    unsigned long long Encoded = Value;

    do
    {
        unsigned char Byte = Encoded & 0x7f;
        Encoded >>= 7;

        if (Encoded != 0)
            Byte |= 0x80;

        Stream->WriteNumeric<LITTLE, BYTE>(Byte);
    } while (Encoded != 0);

    return *this;
}

NetworkCapture& NetworkCapture::operator<<(float const Value)
{
    return *this << static_cast<long long>(Value * 1000);
}

NetworkCapture& NetworkCapture::operator<<(UNetConnection* const Connection)
{
    MILAN_CHECKW(Connection != nullptr, L"");
    MILAN_VERIFYW(Connection->IsA<UTcpipConnection>(), L"");

    auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    return *this << &TcpConn->Address;
}

NetworkCapture& NetworkCapture::operator<<(SOCKADDR_IN* const Socket)
{
    FString const Format = FString::Printf(L"%S:%hu", inet_ntoa(Socket->sin_addr), ntohs(Socket->sin_port));
    return *this << Format;
}

NetworkCapture& NetworkCapture::operator<<(FString& String)
{
    auto const Str = (::game::wstring)String;
    size_t Index = SIZE_MAX;

    auto const Iter = StringLookup.find(Str);
    if (Iter == StringLookup.end())
    {
        // String not in the table, insert.
        Index = StringTable.size();
        StringTable.push_back(Str);
        StringLookup.insert({ Str, Index });
    }
    else
    {
        // String *is* in the table, get the index.
        Index = Iter->second;
    }

    MILAN_CHECKW(Index < INT_MAX, L"");
    *this << static_cast<INT>(Index);

    return *this;
}

NetworkCapture& NetworkCapture::operator<<(FString const& String)
{
    return (*this << const_cast<FString&>(String));
}

NetworkCapture& NetworkCapture::operator<<(SFXName const Name)
{
    auto const Chars = Name.ToString(SFXName::k_formatBasic);
    auto const Index = Name.Number;

    return *this << Chars << Index;
}

NetworkCapture& NetworkCapture::operator<<(UObject* const Object)
{
    return *this << Object->Name;
}

NetworkCapture* NetworkCapture::Global()
{
    return GlobalInstance;
}

void NetworkCapture::GlobalInitialize()
{
    std::lock_guard<std::mutex> const _(GlobalMutex);
    if (GlobalInstance == nullptr)
        GlobalInstance = new NetworkCapture();
}

void NetworkCapture::GlobalClose()
{
    std::lock_guard<std::mutex> const _(GlobalMutex);

    if (GlobalInstance != nullptr)
    {
        auto* Instance = GlobalInstance;
        GlobalInstance = nullptr;
        delete Instance;
    }
}
