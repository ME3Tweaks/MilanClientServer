using Microsoft.EntityFrameworkCore;
using Milan.Core.Data.Model;

namespace Milan.Core.Data;

public sealed class MilanDatabaseContext : DbContext
{
    public DbSet<MatchModel> Matches { get; set; }
    public DbSet<PlayerModel> Players { get; set; }
    public DbSet<PlayerAuthModel> PlayerAuths { get; set; }

    public MilanDatabaseContext(DbContextOptions<MilanDatabaseContext> options)
        : base(options)
    {
        // ...
    }

    public async Task OnServerStart(CancellationToken cancelToken)
    {
        await Database.EnsureCreatedAsync(cancelToken);
        // ... and whatever manual configuration our models will need.
    }

    protected override void ConfigureConventions(ModelConfigurationBuilder configurationBuilder)
    {
        configurationBuilder
            .Properties<TlkStrRef>()
            .HaveConversion<TlkStrRefConverter>();
        configurationBuilder
            .Properties<EMatchResult>()
            .HaveConversion<EMatchResultConverter>();
        configurationBuilder
            .Properties<EMatchState>()
            .HaveConversion<EMatchStateConverter>();
    }

    protected override void OnModelCreating(ModelBuilder modelBuilder)
    {
        modelBuilder.Entity<MatchModel>()
            .HasOne<PlayerModel>(e => e.Creator);

        modelBuilder.Entity<PlayerModel>()
            .HasOne<MatchModel>(e => e.CurrentMatch);
        modelBuilder.Entity<PlayerModel>()
            .HasOne<MatchModel>(e => e.CurrentHostedMatch);

        modelBuilder.Entity<PlayerAuthModel>()
            .HasOne<PlayerModel>(e => e.Player);
    }
}
