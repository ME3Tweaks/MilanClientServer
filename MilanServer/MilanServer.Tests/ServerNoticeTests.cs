using Microsoft.Extensions.Logging.Abstractions;
using Microsoft.Extensions.Options;

namespace MilanServer.Tests;

public sealed class ServerNoticeTests
{
    static MilanLiveOptions Settings(string expected = "", string id = "", params (string Language, string Text)[] texts)
    {
        MilanLiveOptions options = new() { ExpectedGameVersion = expected };
        options.Notice.Id = id;
        foreach ((string language, string text) in texts)
        {
            options.Notice.Text[language] = text;
        }

        return options;
    }

    static ServerNotice Notice(MilanLiveOptions options) => new(new LiveOptions(options), NullLogger<ServerNotice>.Instance);

    [Fact]
    public void BlockedVersions_AreExact_AndToldToUpdate()
    {
        MilanLiveOptions options = Settings("1.4.0");
        options.BlockedGameVersions = [" 1.0.34 ", "1.5.0", "has space", ""];
        ServerNotice notice = Notice(options);

        Assert.True(notice.IsBlocked("1.0.34"));
        Assert.False(notice.IsBlocked("1.0.3"));
        Assert.False(notice.IsBlocked("has space"));
        // Blocked means update, even for a number above the current version.
        Assert.Equal(true, notice.For("1.5.0", "INT").Outdated);
        Assert.Equal(false, notice.For("1.4.0", "INT").Outdated);
    }

    [Theory]
    [InlineData("1.3.2", "1.4.0", -1)]
    [InlineData("1.4", "1.4.0", 0)]
    [InlineData("1.10", "1.9", 1)]
    [InlineData("1.4.0-beta", "1.4.0", 0)]
    [InlineData("2", "1.99.99", 1)]
    public void CompareVersions_ComparesNumericParts(string a, string b, int expected)
    {
        Assert.Equal(expected, ServerNotice.CompareVersions(a, b));
    }

    [Theory]
    [InlineData("milan-1.0.0", "1.0.0")]
    [InlineData("1.0.0", "beta")]
    public void CompareVersions_GivesNoVerdictWithoutNumbers(string a, string b)
    {
        Assert.Null(ServerNotice.CompareVersions(a, b));
    }

    [Fact]
    public void For_TellsOlderGamesToUpdate_AndNotNewerOnes()
    {
        ServerNotice notice = Notice(Settings("1.4.0"));

        Assert.Equal("1.4.0", notice.For("1.3.2", "INT").LatestGameVersion);
        Assert.True(notice.For("1.3.2", "INT").Outdated);
        Assert.False(notice.For("1.4.0", "INT").Outdated);
        Assert.False(notice.For("1.5.0", "INT").Outdated);
        // No version, or one that cannot be compared: no verdict, but the current version is still given.
        Assert.Equal("1.4.0", notice.For(null, null).LatestGameVersion);
        Assert.Null(notice.For(null, null).Outdated);
        Assert.Null(notice.For(null, null).Notice);
        Assert.Null(notice.For("dev build", "INT").Outdated);
    }

    [Fact]
    public void For_PicksTheGamesLanguage_AndFallsBackToInt()
    {
        ServerNotice notice = Notice(Settings(id: "maint", texts: [("INT", "Maintenance"), ("deu", "Wartung")]));

        Assert.Equal(new NoticeText("maint", "Wartung"), notice.For(null, "DEU").Notice);
        Assert.Equal(new NoticeText("maint", "Wartung"), notice.For(null, "deu").Notice);
        Assert.Equal(new NoticeText("maint", "Maintenance"), notice.For(null, "FRA").Notice);
        Assert.Equal(new NoticeText("maint", "Maintenance"), notice.For(null, "not a language").Notice);
        Assert.Null(notice.For(null, null).LatestGameVersion);
    }

    [Fact]
    public void For_HasNoNoticeWithoutText_OrWithoutAnIntFallback()
    {
        Assert.Null(Notice(Settings(id: "unused")).For(null, "INT").Notice);
        Assert.Null(Notice(Settings(texts: [("DEU", "Wartung")])).For(null, "FRA").Notice);
    }

    [Fact]
    public void Id_IsMadeFromTheText_UnlessOneIsSet()
    {
        string first = Notice(Settings(texts: [("INT", "Maintenance")])).For(null, null).Notice!.Id;
        string same = Notice(Settings(texts: [("INT", "Maintenance")])).For(null, null).Notice!.Id;
        string edited = Notice(Settings(texts: [("INT", "Maintenance Saturday")])).For(null, null).Notice!.Id;

        Assert.Equal(first, same);
        Assert.NotEqual(first, edited);
        Assert.Equal(16, first.Length);
        Assert.Equal("maint-1109", Notice(Settings(id: "maint-1109", texts: [("INT", "Maintenance")])).For(null, null).Notice!.Id);
        // An Id a game could not store is replaced by the one made from the text.
        Assert.Equal(first, Notice(Settings(id: "has spaces", texts: [("INT", "Maintenance")])).For(null, null).Notice!.Id);
    }

    [Fact]
    public void Text_IsCleanedForAMessageBox()
    {
        ServerNotice notice = Notice(Settings(texts: [("INT", "  Line one\\nLine two\r\nLine three\u0007  "), ("DEU", new string('x', ServerNotice.MaxTextLength + 1))]));

        Assert.Equal("Line one\nLine two\nLine three", notice.For(null, "INT").Notice!.Text);
        // Too long: dropped, so the game falls back to INT.
        Assert.Equal("Line one\nLine two\nLine three", notice.For(null, "DEU").Notice!.Text);
    }

    [Fact]
    public void InvalidExpectedGameVersion_IsIgnored()
    {
        ServerNotice notice = Notice(Settings("has spaces"));

        Assert.Equal("", notice.ExpectedGameVersion);
        Assert.Null(notice.For("1.0.0", null).LatestGameVersion);
    }

    [Fact]
    public void Edits_ApplyWithoutARestart()
    {
        LiveOptions options = new(Settings("1.3.0", texts: [("INT", "Old")]));
        ServerNotice notice = new(options, NullLogger<ServerNotice>.Instance);
        Assert.False(notice.For("1.3.0", null).Outdated);

        options.Change(Settings("1.4.0", texts: [("INT", "New")]));

        Assert.True(notice.For("1.3.0", null).Outdated);
        Assert.Equal("New", notice.For(null, null).Notice!.Text);
        Assert.Equal("1.4.0", notice.ExpectedGameVersion);
    }
}
