# EN-09 — monta o diretorio de distribuicao e o instalador NSIS.
#
#   packaging\windows\build-installer.ps1 -FfmpegRoot C:\ffmpeg [-Saida dist\windows]
#
# Espera Qt e MSVC no PATH. Nao baixa nada: o que ele faz e reunir o binario,
# as bibliotecas do Qt (via windeployqt), as DLLs do FFmpeg e os dados do
# player num diretorio que roda por si so.

param(
    [Parameter(Mandatory = $true)][string]$FfmpegRoot,
    # Onde mora zlib1.dll. Vazio quando o zlib do sistema for estatico ou ja
    # estiver no PATH; a conferencia no fim do script avisa se faltar.
    [string]$ZlibRoot = $env:ZLIB_ROOT,
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

# O zlib nao e do Qt nem do FFmpeg, entao nenhum dos dois o traz. O leitor de
# .wsz depende dele.
# Copia tudo o que houver ali: o nome do arquivo varia conforme quem construiu
# o zlib — o do vcpkg se chama z.dll, e nao zlib1.dll, e um glob por "zlib*"
# deixava o pacote sem ele.
if ($ZlibRoot -and (Test-Path "$ZlibRoot\bin")) {
    Copy-Item "$ZlibRoot\bin\*.dll" $Saida
}

# windeployqt resolve Qt: plugins de plataforma, estilos e dependencias.
windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler `
            "$Saida\playampng.exe"

# Runtime do MSVC ao lado do executavel.
#
# O windeployqt resolve isso deixando o vc_redist.exe no diretorio, o que aqui
# nao serve: instalar o redistribuivel pede administrador, e este instalador
# nao pede. Sem o runtime, numa maquina que nunca teve Visual Studio o player
# nao abre — e o sintoma e o mesmo 0xC0000135 que nao nomeia a biblioteca.
# A Microsoft admite o modo "app-local": as DLLs ao lado do binario.
$redist = Get-ChildItem "$env:VCToolsRedistDir\x64\Microsoft.VC*.CRT\*.dll" -ErrorAction SilentlyContinue
if ($redist) {
    Copy-Item $redist.FullName $Saida
    Remove-Item "$Saida\vc_redist*.exe" -ErrorAction SilentlyContinue
    Write-Host "runtime do MSVC: $($redist.Count) DLL(s) ao lado do executavel"
} else {
    Write-Warning "runtime do MSVC nao encontrado; o pacote depende de a maquina ja te-lo"
}

# Conferencia de dependencias.
#
# Uma DLL que falta nao da erro na montagem: da erro na MAQUINA DO USUARIO, na
# forma de um 0xC0000135 que nem diz qual biblioteca falta. Foi assim que dez
# testes morreram no CI ate alguem ler o codigo de saida. Conferir aqui custa
# dez linhas e transforma isso numa falha com nome, antes de empacotar.
#
# Cobre o que o executavel importa diretamente; as dependencias das proprias
# DLLs do Qt vem com elas pelo windeployqt.
$sistema = Join-Path $env:WINDIR "System32"
$dependencias = & dumpbin /nologo /dependents "$Saida\playampng.exe" |
                Select-String -Pattern '^\s+(\S+\.dll)$' |
                ForEach-Object { $_.Matches[0].Groups[1].Value }
# Os conjuntos de API (api-ms-win-*, ext-ms-*) sao nomes virtuais resolvidos
# pelo proprio Windows; nao existem como arquivo e procura-los da falso
# positivo.
$faltando = $dependencias |
    Where-Object { $_ -notlike "api-ms-win-*" -and $_ -notlike "ext-ms-*" } |
    Where-Object {
        -not (Test-Path (Join-Path $Saida $_)) -and -not (Test-Path (Join-Path $sistema $_))
    }
if ($faltando) { throw "DLL ausente no pacote: $($faltando -join ', ')" }
Write-Host "dependencias conferidas: $($dependencias.Count), nenhuma faltando"

Write-Host "`ndiretorio de distribuicao:"
Get-ChildItem $Saida | Select-Object Name, Length | Format-Table

# O instalador e opcional: sem NSIS, o diretorio acima ja roda.
#
# O pacote do Chocolatey instala o NSIS mas nao o poe no PATH, entao procurar
# so por Get-Command dava "nao encontrado" com o NSIS instalado do lado.
$makensis = (Get-Command makensis -ErrorAction SilentlyContinue).Source
if (-not $makensis) {
    $makensis = @("$env:ProgramFiles\NSIS\makensis.exe",
                  "${env:ProgramFiles(x86)}\NSIS\makensis.exe") |
                Where-Object { Test-Path $_ } | Select-Object -First 1
}
if ($makensis) {
    & $makensis "/DVERSION=$versao" "/DSOURCE_DIR=$((Resolve-Path $Saida).Path)" `
                packaging\windows\playampng.nsi
    if ($LASTEXITCODE -ne 0) { throw "makensis falhou com codigo $LASTEXITCODE" }
    Write-Host "instalador: packaging\windows\PlayAmpNG-$versao-setup.exe"
} else {
    Write-Warning "makensis nao encontrado: instalador nao gerado, diretorio de distribuicao pronto"
}
