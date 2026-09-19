#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct AVFormatContext;

namespace pang::core {

// Propriedades que a fonte realmente informou.
//
// Os campos opcionais existem porque a especificacao (PL-26, MD-09) proibe
// inventar duracao, bitrate ou progresso quando a fonte nao os fornece. Um
// stream ao vivo chega aqui com duration_us e bitrate_bps vazios, e e assim
// que a interface deve exibi-lo.
struct ProbeResult {
    std::string format;  // nome longo do conteiner
    std::string codec;   // decodificador do fluxo de audio escolhido
    int sample_rate = 0;
    int channels = 0;
    std::optional<std::int64_t> duration_us;
    std::optional<std::int64_t> bitrate_bps;
    bool seekable = false;  // false desabilita a busca temporal (PL-23)

    // MD-09 — fonte ao vivo: sem duracao e sem busca. A interface nunca pode
    // exibi-la como arquivo de duracao finita, nem mesmo estimando.
    bool live = false;

    // MD-05 — nome da estacao, quando o servidor ICY o envia.
    std::optional<std::string> station;

    // AU-15 — lidos das tags. Ausentes quando a fonte nao traz: o projeto le
    // ReplayGain, nao calcula (calcular seria um scanner, fora de escopo).
    std::optional<float> replaygain_track_db;
    std::optional<float> replaygain_album_db;
};

// Deriva as propriedades de uma fonte JA ABERTA.
//
// Existe como ponto unico porque probe() e Decoder::open() precisam do mesmo
// resultado, e enquanto cada um derivava por conta propria eles divergiram: o
// decodificador nunca marcava `live` nem `station`, e um stream tocava como se
// fosse arquivo. Duas copias da mesma regra sempre acabam assim.
ProbeResult describe(AVFormatContext* fmt, int stream_index, const std::string& url);

// Abre url (caminho local ou HTTP/HTTPS) e le as propriedades do melhor fluxo
// de audio. Em falha devolve nullopt e preenche error.
std::optional<ProbeResult> probe(const std::string& url, std::string& error);

// Licenca efetiva do FFmpeg com que este binario foi vinculado.
//
// Existe porque o build do FFmpeg decide a licenca do produto inteiro: um
// FFmpeg com --enable-gpl torna o binario distribuido GPL. Verificar isso em
// tempo de execucao evita descobrir o problema no empacotamento (M7).
std::string ffmpeg_license();
std::string ffmpeg_version();

}  // namespace pang::core
