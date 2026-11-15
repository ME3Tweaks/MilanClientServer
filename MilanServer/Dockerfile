# MilanServer container image. Build from the MilanServer directory:
#   docker build --pull -t milanserver .
# For an arm64 server, add --platform linux/arm64; the SDK still runs natively.
# Run the unit tests inside the SDK image instead:
#   docker build --target test .

# Use the serviced 10.0 tag and --pull: newly announced patch tags may not be
# published in MCR yet. Record the tested image digest for production rollouts.
FROM --platform=$BUILDPLATFORM mcr.microsoft.com/dotnet/sdk:10.0 AS build
ARG TARGETARCH
WORKDIR /src
COPY global.json MilanServer.slnx ./
COPY MilanServer/MilanServer.csproj MilanServer/
COPY MilanServer.Tests/MilanServer.Tests.csproj MilanServer.Tests/
RUN dotnet restore MilanServer.slnx && dotnet restore MilanServer/MilanServer.csproj -a $TARGETARCH
COPY MilanServer/ MilanServer/
COPY MilanServer.Tests/ MilanServer.Tests/
# Trimmed and self-contained, so the runtime image needs no .NET install.
RUN dotnet publish MilanServer/MilanServer.csproj -c Release -a $TARGETARCH --self-contained --no-restore -o /app

FROM build AS test
RUN dotnet test --solution MilanServer.slnx -c Release

# Chiseled Ubuntu: only the native libraries .NET needs, no shell or package manager.
FROM mcr.microsoft.com/dotnet/runtime-deps:10.0-noble-chiseled AS runtime
WORKDIR /app
COPY --from=build /app ./
# HTTP control API. UDP endpoint discovery uses Milan__UdpPort (default 5081); clients
# read that number from /v1/info, so publish it unchanged.
ENV ASPNETCORE_HTTP_PORTS=5080
EXPOSE 5080/tcp
EXPOSE 5081/udp
USER $APP_UID
ENTRYPOINT ["./MilanServer"]
