[Environment]::CurrentDirectory = Get-Location

foreach ($diff in $(Get-ChildItem -Filter '*.diff').Name) {
    $diff = [IO.Path]::GetFullPath($diff)
    $contents = Get-Content -Raw $diff
    $encoding = New-Object System.Text.UTF8Encoding $False
    [System.IO.File]::WriteAllText($diff, $contents, $encoding)
}
