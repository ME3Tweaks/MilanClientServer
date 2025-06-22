using Milan.Core.Payload;

namespace Milan.Core.Tests.Payload;

public class JsonPayloadReaderTest
{
    [Fact]
    public void AcceptsValidPayload()
    {
        string json = """
            {
                "intField": 42,
                "floatField": 2.0,
                "stringField": "cheers",
                "boolField": true,
                "objectField": {
                    "hello": "there",
                    "general": "kenobi"
                },
                "intArrayField": [
                    1, 3, 5, 7, 11, 13, 17, 19, 23
                ],
                "stringArrayField": [
                    "a", "b", "c"
                ],
            }
            """;

        int intValue = default;
        float floatValue = default;
        string stringValue = string.Empty;
        bool boolValue = default;
        string objectHelloValue = string.Empty;
        string objectGeneralValue = string.Empty;
        List<int> intArrayValue = [];
        List<string> stringArrayValue = [];

        JsonPayloadReader payload = new(json);
        payload.GetRootWith((ref reader) =>
        {
            intValue = reader.GetInt("intField");
            floatValue = reader.GetFloat("floatField");
            stringValue = reader.GetString("stringField");
            boolValue = reader.GetBool("boolField");
            reader.GetObjectWith("objectField", (ref reader) =>
            {
                objectHelloValue = reader.GetString("hello");
                objectGeneralValue = reader.GetString("general");
            });
            reader.GetArrayWith("intArrayField", (ref reader) =>
            {
                Assert.Equal(9, reader.CountItems());

                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
                intArrayValue.Add(reader.NextInt());
            });
            reader.GetArrayWith("stringArrayField", (ref reader) =>
            {
                Assert.Equal(3, reader.CountItems());

                stringArrayValue.Add(reader.NextString());
                stringArrayValue.Add(reader.NextString());
                stringArrayValue.Add(reader.NextString());
            });
        });

        Assert.Equal(42, intValue);
        Assert.Equal(2.0f, floatValue);
        Assert.Equal("cheers", stringValue);
        Assert.True(boolValue);
        Assert.Equal("there", objectHelloValue);
        Assert.Equal("kenobi", objectGeneralValue);
        Assert.Equal(intArrayValue, [1, 3, 5, 7, 11, 13, 17, 19, 23]);
        Assert.Equal(stringArrayValue, ["a", "b", "c"]);
    }

    [Fact]
    public void AcceptsArrayOfObjects()
    {
        string json = """
            {
                "array": [
                    { "int": 1, "float": 1.0, "string": "one" },
                    { "int": 2, "float": 2.0, "string": "two" }
                ],
                "stringAfter": "done"
            }
            """;

        JsonPayloadReader payload = new(json);
        payload.GetRootWith((ref reader) =>
        {
            reader.GetArrayWith("array", (ref reader) =>
            {
                Assert.Equal(2, reader.CountItems());

                reader.NextObjectWith((ref reader) =>
                {
                    Assert.Equal(1, reader.GetInt("int"));
                    Assert.Equal(1.0f, reader.GetFloat("float"));
                    Assert.Equal("one", reader.GetString("string"));
                });
                reader.NextObjectWith((ref reader) =>
                {
                    Assert.Equal(2, reader.GetInt("int"));
                    Assert.Equal(2.0f, reader.GetFloat("float"));
                    Assert.Equal("two", reader.GetString("string"));
                });
            });

            Assert.Equal("done", reader.GetString("stringAfter"));
        });
    }

    [Fact]
    public void AcceptsEmptyArray()
    {
        string json = """
            { "array": [] }
            """;

        JsonPayloadReader payload = new(json);
        payload.GetRootWith((ref reader) =>
        {
            reader.GetArrayWith("array", (ref reader) =>
            {
                Assert.Equal(0, reader.CountItems());
            });
        });
    }

    [Fact]
    public void HandlesIncompleteObjectReads()
    {
        string json = """
            {
                "first": "one",
                "second": "two",
                "third": {
                    "hello": 1,
                    "there": 2,
                },
                "fourth": "four",
                "fifth": null
            }
            """;

        JsonPayloadReader payload = new(json);
        payload.GetRootWith((ref reader) =>
        {
            Assert.Equal("one", reader.GetString("first"));
            Assert.Equal("two", reader.GetString("second"));
            reader.GetObjectWith("third", (ref reader) =>
            {
                Assert.Equal(1, reader.GetInt("hello"));
                // missing: there => 2
            });
            Assert.Equal("four", reader.GetString("fourth"));
            // missing: fifth => null
        });
    }
}
