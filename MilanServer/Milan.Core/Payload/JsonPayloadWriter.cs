using Milan.Core.Utility;
using System.Runtime.CompilerServices;
using System.Text.Json;

namespace Milan.Core.Payload;

public ref struct JsonPayloadWriter
{
    private static readonly JsonWriterOptions s_writerOptions = new()
    {
        MaxDepth = 8,
    };

    private static readonly JsonWriterOptions s_writerPrettyOptions = new()
    {
        Indented = true,
        IndentSize = 4,
        MaxDepth = 8,
        NewLine = "\n",
    };

    private Utf8JsonWriter _writer;

    public JsonPayloadWriter(Stream stream, bool prettyFormat = false)
    {
        if (!stream.CanWrite) throw new ArgumentException("output stream not writeable", nameof(stream));
        _writer = new(stream, prettyFormat ? s_writerPrettyOptions : s_writerOptions);
    }

    public void SetRootWith(RefSingleAction<JsonObjectWriter> callback)
    {
        WriteObjectWith(callback);
        _writer.Flush();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly void BeginWriteField(string key) => _writer.WritePropertyName(key);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly void WriteInt(int value) => _writer.WriteNumberValue(value);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly void WriteFloat(float value) => _writer.WriteNumberValue(value);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly void WriteString(string value) => _writer.WriteStringValue(value);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly void WriteBool(bool value) => _writer.WriteBooleanValue(value);

    [MethodImpl (MethodImplOptions.AggressiveInlining)]
    internal void WriteObjectWith(RefSingleAction<JsonObjectWriter> callback)
    {
        _writer.WriteStartObject();

        JsonObjectWriter objectWriter = new(this);
        callback(ref objectWriter);

        // Propagate updated writer state.
        _writer = objectWriter._payload._writer;

        _writer.WriteEndObject();
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void WriteArrayWith(RefSingleAction<JsonArrayWriter> callback)
    {
        _writer.WriteStartArray();

        JsonArrayWriter arrayWriter = new(this);
        callback(ref arrayWriter);

        // Propagate updated writer state.
        _writer = arrayWriter._payload._writer;

        _writer.WriteEndArray();
    }
}

public ref struct JsonObjectWriter
{
    internal JsonPayloadWriter _payload;
    internal JsonObjectWriter(JsonPayloadWriter payload) => _payload = payload;

    public readonly void SetInt(string key, int value)
    {
        _payload.BeginWriteField(key);
        _payload.WriteInt(value);
    }

    public readonly void SetFloat(string key, float value)
    {
        _payload.BeginWriteField(key);
        _payload.WriteFloat(value);
    }

    public readonly void SetString(string key, string value)
    {
        _payload.BeginWriteField(key);
        _payload.WriteString(value);
    }

    public readonly void SetBool(string key, bool value)
    {
        _payload.BeginWriteField(key);
        _payload.WriteBool(value);
    }

    public readonly void SetObjectWith(string key, RefSingleAction<JsonObjectWriter> callback)
    {
        _payload.BeginWriteField(key);
        _payload.WriteObjectWith(callback);
    }

    public readonly void SetArrayWith(string key, RefSingleAction<JsonArrayWriter> callback)
    {
        _payload.BeginWriteField(key);
        _payload.WriteArrayWith(callback);
    }
}

public ref struct JsonArrayWriter
{
    internal JsonPayloadWriter _payload;
    internal JsonArrayWriter(JsonPayloadWriter payload) => _payload = payload;

    public readonly void PushInt(int value) => _payload.WriteInt(value);
    public readonly void PushFloat(float value) => _payload.WriteFloat(value);
    public readonly void PushString(string value) => _payload.WriteString(value);
    public readonly void PushBool(bool value) => _payload.WriteBool(value);
    public readonly void PushObjectWith(RefSingleAction<JsonObjectWriter> callback) => _payload.WriteObjectWith(callback);
    public readonly void PushArrayWith(RefSingleAction<JsonArrayWriter> callback) => _payload.WriteArrayWith(callback);
}
