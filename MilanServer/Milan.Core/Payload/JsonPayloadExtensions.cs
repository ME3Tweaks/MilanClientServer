namespace Milan.Core.Payload;

public static class JsonObjectReaderExtension
{
    public static Guid GetGuid(this JsonObjectReader self, string key)
    {
        string value = self.GetString(key);
        return new Guid(value);
    }

    public static byte[] GetBlob(this JsonObjectReader self, string key)
    {
        string value = self.GetString(key);
        return Convert.FromBase64String(value);
    }
}

public static class JsonArrayReaderExtension
{
    public static Guid NextGuid(this JsonArrayReader self)
    {
        string value = self.NextString();
        return new Guid(value);
    }

    public static byte[] NextBlob(this JsonArrayReader self)
    {
        string value = self.NextString();
        return Convert.FromBase64String(value);
    }
}

public static class JsonObjectWriterExtension
{
    public static void SetGuid(this JsonObjectWriter self, string key, Guid value)
        => self.SetString(key, value.ToString());

    public static void SetBlob(this JsonObjectWriter self, string key, byte[] value)
        => self.SetString(key, Convert.ToBase64String(value));
}

public static class JsonArrayWriterExtension
{
    public static void PushGuid(this JsonArrayWriter self, Guid value)
        => self.PushString(value.ToString());

    public static void PushBlob(this JsonArrayWriter self, byte[] value)
        => self.PushString(Convert.ToBase64String(value));
}
