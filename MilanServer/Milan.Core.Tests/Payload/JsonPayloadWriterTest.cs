using Milan.Core.Payload;
using System.Text;

namespace Milan.Core.Tests.Payload;

public class JsonPayloadWriterTest
{
    [Fact]
    public void CanWritePayload()
    {
        MemoryStream stream = new();
        JsonPayloadWriter writer = new(stream, false);

        writer.SetRootWith((ref writer) =>
        {
            writer.SetInt("intField", 42);
            writer.SetFloat("floatField", 2.0f);
            writer.SetString("stringField", "hi");
            writer.SetBool("boolField", true);
            writer.SetObjectWith("objectField", (ref writer) =>
            {
                writer.SetString("a", "b");
            });
            writer.SetArrayWith("intArrayField", (ref writer) =>
            {
                writer.PushInt(1);
                writer.PushInt(3);
                writer.PushInt(5);
            });
        });

        string json = """{"intField":42,"floatField":2,"stringField":"hi","boolField":true,"objectField":{"a":"b"},"intArrayField":[1,3,5]}""";
        string serialized = Encoding.UTF8.GetString(stream.ToArray());

        Assert.Equal(json, serialized);
    }
}
