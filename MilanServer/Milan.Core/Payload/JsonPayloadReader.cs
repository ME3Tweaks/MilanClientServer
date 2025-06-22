using Milan.Core.Utility;
using System.Buffers;
using System.Runtime.CompilerServices;
using System.Text;
using System.Text.Json;

namespace Milan.Core.Payload;

public ref struct JsonPayloadReader(ReadOnlySpan<byte> contents)
{
    private static readonly JsonReaderOptions s_readerOptions = new()
    {
        AllowTrailingCommas = true,
        CommentHandling = JsonCommentHandling.Skip,
        MaxDepth = 8,
    };

    private Utf8JsonReader _reader = new(contents, s_readerOptions);

    internal readonly string? CurrentTokenString
    {
        get
        {
            try
            {
                return Encoding.UTF8.GetString(
                    _reader.HasValueSequence
                        ? _reader.ValueSequence.ToArray()
                        : _reader.ValueSpan);
            }
            catch (ArgumentException)
            {
                return null;
            }
        }
    }

    internal readonly JsonTokenType CurrentTokenType
        => _reader.TokenType;

    public readonly long BytePosition => _reader.BytesConsumed;

    public JsonPayloadReader(string contents)
        : this(Encoding.UTF8.GetBytes(contents))
    {
        // ...
    }

    public void GetRootWith(RefSingleAction<JsonObjectReader> callback)
    {
        if (!TryGetObjectWith(callback))
            throw MakeCommonException("invalid root object");
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly JsonPayloadException MakeCommonException(string message)
        => new(_reader.BytesConsumed, message);

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly JsonPayloadException MakeFieldValueException(string key, string type)
        => MakeCommonException($"failed to read field '{key}' value of type {type}");

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly JsonPayloadException MakeItemValueException(string type)
        => MakeCommonException($"failed to read item value of type {type}");

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal void BeginReadField(string key)
    {
        if (!_reader.Read() || _reader.TokenType != JsonTokenType.PropertyName)
            throw MakeCommonException($"failed to read field '{key}' key");

        if (!_reader.ValueTextEquals(key))
        {
            string found = CurrentTokenString ?? "<n/a>";
            throw MakeCommonException($"expected field '{key}', found '{found}'");
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryCountArrayItems(out int count)
    {
        int realCount = 0;
        Utf8JsonReader stored = _reader;

        try
        {
            _reader.Read();
            while (_reader.TokenType != JsonTokenType.EndArray)
            {
                switch (_reader.TokenType)
                {
                    case JsonTokenType.StartObject:
                    case JsonTokenType.StartArray:
                        realCount++;
                        _reader.Skip();
                        _reader.Read();  // consume object / array terminator
                        continue;
                    case JsonTokenType.String:
                    case JsonTokenType.Number:
                    case JsonTokenType.True:
                    case JsonTokenType.False:
                    case JsonTokenType.Null:
                        realCount++;
                        _reader.Read();
                        continue;
                }
            }

            count = realCount;
            return true;
        }
        catch
        {
            count = default;
            return false;
        }
        finally
        {
            _reader = stored;
        }
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal readonly bool IsTokenOf(JsonTokenType type)
        => _reader.TokenType == type;

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryGetInt(out int value)
    {
        value = default;
        return _reader.Read()
            && IsTokenOf(JsonTokenType.Number)
            && _reader.TryGetInt32(out value);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryGetFloat(out float value)
    {
        value = default;
        return _reader.Read()
            && IsTokenOf(JsonTokenType.Number)
            && _reader.TryGetSingle(out value);
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryGetString(out string value)
    {
        value = string.Empty;
        if (_reader.Read() && IsTokenOf(JsonTokenType.String))
        {
            value = _reader.GetString()!;
            return true;
        }
        return false;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryGetBool(out bool value)
    {
        if (_reader.Read())
        {
            switch (_reader.TokenType)
            {
                case JsonTokenType.True:
                    value = true;
                    return true;
                case JsonTokenType.False:
                    value = false;
                    return true;
            }
        }

        value = default;
        return false;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryTerminateParent(JsonTokenType terminator)
    {
        if (_reader.Read() && _reader.TokenType == terminator)
            return true;

        int depth = _reader.CurrentDepth;
        while (_reader.CurrentDepth >= depth)
        {
            if (!_reader.Read())
                break;

            if (_reader.TokenType == terminator)
                return true;
        }

        return false;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryGetObjectWith(RefSingleAction<JsonObjectReader> callback)
    {
        if (_reader.Read())
        {
            if (_reader.TokenType == JsonTokenType.StartObject)
            {
                JsonObjectReader objectReader = new(this);
                callback(ref objectReader);

                // Propagate updated reader state.
                _reader = objectReader._payload._reader;

                if (TryTerminateParent(JsonTokenType.EndObject))
                    return true;
            }
        }

        return false;
    }

    [MethodImpl(MethodImplOptions.AggressiveInlining)]
    internal bool TryGetArrayWith(RefSingleAction<JsonArrayReader> callback)
    {
        if (_reader.Read())
        {
            if (_reader.TokenType == JsonTokenType.StartArray)
            {
                JsonArrayReader arrayReader = new(this);
                callback(ref arrayReader);

                // Propagate updated reader state.
                _reader = arrayReader._payload._reader;

                if (TryTerminateParent(JsonTokenType.EndArray))
                    return true;
            }
        }

        return false;
    }
}

public ref struct JsonObjectReader
{
    internal JsonPayloadReader _payload;
    internal JsonObjectReader(JsonPayloadReader payload) => _payload = payload;

    public int GetInt(string key)
    {
        _payload.BeginReadField(key);
        if (_payload.TryGetInt(out int value)) return value;
        throw _payload.MakeFieldValueException(key, "int");
    }

    public float GetFloat(string key)
    {
        _payload.BeginReadField(key);
        if (_payload.TryGetFloat(out float value)) return value;
        throw _payload.MakeFieldValueException(key, "float");
    }

    public string GetString(string key)
    {
        _payload.BeginReadField(key);
        if (_payload.TryGetString(out string value)) return value;
        throw _payload.MakeFieldValueException(key, "string");
    }

    public bool GetBool(string key)
    {
        _payload.BeginReadField(key);
        if (_payload.TryGetBool(out bool value)) return value;
        throw _payload.MakeFieldValueException(key, "bool");
    }

    public void GetObjectWith(string key, RefSingleAction<JsonObjectReader> callback)
    {
        _payload.BeginReadField(key);
        if (!_payload.TryGetObjectWith(callback))
            throw _payload.MakeFieldValueException(key, "object");
    }

    public void GetArrayWith(string key, RefSingleAction<JsonArrayReader> callback)
    {
        _payload.BeginReadField(key);
        if (!_payload.TryGetArrayWith(callback))
            throw _payload.MakeFieldValueException(key, "array");
    }
}

public ref struct JsonArrayReader
{
    internal JsonPayloadReader _payload;
    internal JsonArrayReader(JsonPayloadReader payload) => _payload = payload;

    public int CountItems()
    {
        if (_payload.CurrentTokenType != JsonTokenType.StartArray)
            throw new InvalidOperationException("can only count array items before any have been parsed");

        if (!_payload.TryCountArrayItems(out int count))
            throw _payload.MakeCommonException("invalid array");

        return count;
    }

    public int NextInt()
    {
        if (_payload.TryGetInt(out int value)) return value;
        throw _payload.MakeItemValueException("int");
    }

    public float NextFloat()
    {
        if (_payload.TryGetFloat(out float value)) return value;
        throw _payload.MakeItemValueException("float");
    }

    public string NextString()
    {
        if (_payload.TryGetString(out string value)) return value;
        throw _payload.MakeItemValueException("string");
    }

    public bool NextBool()
    {
        if (_payload.TryGetBool(out bool value)) return value;
        throw _payload.MakeItemValueException("bool");
    }

    public void NextObjectWith(RefSingleAction<JsonObjectReader> callback)
    {
        if (!_payload.TryGetObjectWith(callback))
            throw _payload.MakeItemValueException("object");
    }

    public void NextArrayWith(RefSingleAction<JsonArrayReader> callback)
    {
        if (!_payload.TryGetArrayWith(callback))
            throw _payload.MakeItemValueException("object");
    }
}
