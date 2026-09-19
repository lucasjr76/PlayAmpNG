# EN-09 — monta o diretorio de distribuicao e o instalador NSIS.
#
#   packaging\windows\build-installer.ps1 -FfmpegRoot C:\ffmpeg [-Saida dist\windows]
#
# Espera Qt e MSVC no PATH. Nao baixa nada: o que ele faz e reunir o binario,
# as bibliotecas do Qt (via windeployqt), as DLLs do FFmpeg e os dados do
# player num diretorio que roda por si so.

param(
    [Parameter(Mandatory = $true)][string]$FfmpegRoot,
    [string]$Saida = "dist\windows",
    [string]$BuildDir = "build-windows"
)

$ErrorActionPreference = "Stop"
$raiz = Resolve-Path (Join-Path $PSScriptRoot "..\..")
Set-Location $raiz

$versao = (Select-String -Path CMakeLists.txt -Pattern 'VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)' |
           Select-Object -First 1).Matches.Groups[1].Value
Write-Host "versao $versao"

cmake -S . -B $BuildDir -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo `
      "-DCMAKE_PREFIX_PATH=$FfmpegRoot;$env:CMAKE_PREFIX_PATH"
cmake --build $BuildDir --target playampng

if (Test-Path $Saida) { Remove-Item -Recurse -Force $Saida }
New-Item -ItemType Directory -Force -Path $Saida | Out-Null

Copy-Item "$BuildDir\playampng.exe" $Saida
Copy-Item LICENSE $Saida
Copy-Item "assets\skin\font\OFL.txt" "$Saida\OFL-Silkscreen.txt"

# O skin vai ao lado do binario. O player procura nessa ordem:
# ..\share\playampng\skin\default, depois .\skin\default — e a segunda e a que
# vale aqui, porque no Windows nao ha prefixo de instalacao.
New-Item -ItemType Directory -Force -Path "$Saida\skin" | Out-Null
Copy-Item -Recurse "assets\skin\default" "$Saida\skin\default"

# DLLs do FFmpeg. Sem elas o executavel nem inicia, e o erro do Windows nao
# diz qual biblioteca falta.
Copy-Item "$FfmpegRoot\bin\*.dll" $Saida

# windeployqt resolve Qt: plugins de plataforma, estilos e dependencias.
windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler `
            "$Saida\playampng.exe"

Write-Host "`ndiretorio de distribuicao:"
Get-ChildItem $Saida | Select-Object Name, Length | Format-Table

# O instalador e opcional: sem NSIS, o diretorio acima ja roda.
if (Get-Command makensis -ErrorAction SilentlyContinue) {
    makensis "/DVERSION=$versao" "/DSOURCE_DIR=$((Resolve-Path $Saida).Path)" `
             packaging\windows\playampng.nsi
    Write-Host "instalador: packaging\windows\PlayAmpNG-$versao-setup.exe"
} else {
    Write-Warning "makensis nao encontrado: instalador nao gerado, diretorio de distribuicao pronto"
}
