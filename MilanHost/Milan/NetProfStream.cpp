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

    StringTable.reserve(100);
    StringLookup.reserve(100);
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

void NetworkCapture::CaptureActorReplication(UNetConnection* const Connection, AActor* const Actor)
{
    *this << NPRecord::ActorReplication;
    *this << Connection << Actor->StaticFullName();
}

void NetworkCapture::CapturePropertyReplication(UNetConnection* const Connection, UProperty* const Property, INT const Bits)
{
    *this << NPRecord::PropertyReplication;
    *this << Connection << Property->StaticFullName() << Bits;
}

void NetworkCapture::RecordStringTable_NoLock()
{
    if (StringTierOffset >= StringTable.size())
    {
        // Let's avoid empty records.
        return;
    }

    *this << NPRecord::TieredStringLookup;
    Stream->WriteNumeric<LITTLE, INT>(static_cast<int>(StringTable.size() - StringTierOffset) - 1);
    for (size_t i = StringTierOffset; i < StringTable.size(); ++i)
    {
        MILAN_CHECKW(StringTable[i].length() < UINT16_MAX, L"");
        Stream->WriteNumeric<LITTLE, SHORT>(static_cast<short>(StringTable[i].length()));
        Stream->WriteExact(std::span{ (unsigned char*)StringTable[i].data(), StringTable[i].length() * 2 });
    }

    StringTierOffset = StringTable.size() - 1;
}

NetworkCapture& NetworkCapture::operator<<(NPRecord const Type)
{
    MILAN_CHECKW(Type < NPRecord::MAX, L"");
    Stream->WriteNumeric<LITTLE>(static_cast<unsigned char>(Type));
    return *this;
}

NetworkCapture& NetworkCapture::operator<<(UNetConnection* const Connection)
{
    MILAN_CHECKW(Connection != nullptr, L"");
    MILAN_VERIFYW(Connection->IsA<UTcpipConnection>(), L"");

    auto const TcpConn = reinterpret_cast<UTcpipConnection_Proxy*>(Connection);
    *this << TcpConn->FormatRemoteAddress();

    return *this;
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

NetworkCapture& NetworkCapture::operator<<(INT const Value)
{
    Stream->WriteNumeric<LITTLE>(Value);
    return *this;
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
        delete GlobalInstance;
        GlobalInstance = nullptr;
    }
}
