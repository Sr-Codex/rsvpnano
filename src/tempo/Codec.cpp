#include "tempo/Codec.h"

#include <glaze/json.hpp>

#include <cstdint>
#include <cstdio>

namespace tempo {
    namespace {

        constexpr glz::opts kLeitura{.error_on_unknown_keys = false};

        // Decodifica um codepoint UTF-8; em byte invalido devolve U+FFFD e avanca 1 byte.
        uint32_t proximo(std::string_view& texto) {
            const auto b0 = static_cast<uint8_t>(texto.front());
            size_t n = 1;
            uint32_t cp = b0;
            if (b0 >= 0xF0 && b0 < 0xF8) {
                n = 4;
                cp = b0 & 0x07U;
            } else if (b0 >= 0xE0) {
                n = 3;
                cp = b0 & 0x0FU;
            } else if (b0 >= 0xC0) {
                n = 2;
                cp = b0 & 0x1FU;
            } else if (b0 >= 0x80) {
                texto.remove_prefix(1);
                return 0xFFFD;
            }
            if (b0 >= 0xF8 || texto.size() < n) {
                texto.remove_prefix(1);
                return 0xFFFD;
            }
            for (size_t i = 1; i < n; ++i) {
                const auto b = static_cast<uint8_t>(texto[i]);
                if ((b & 0xC0U) != 0x80U) {
                    texto.remove_prefix(1);
                    return 0xFFFD;
                }
                cp = (cp << 6) | (b & 0x3FU);
            }
            texto.remove_prefix(n);
            return cp;
        }

        // Latin (U+00A0..U+024F) + pontuacao geral comum + alguns simbolos; tudo o mais (emoji,
        // dingbats, matematicos, variation selectors, ZWJ) e descartado.
        bool glyphSuportado(uint32_t cp) {
            if (cp == '\t' || cp == '\n' || cp == '\r') return true;  // viram espaco
            if (cp < 0x20 || cp == 0x7F) return false;
            if (cp < 0x7F) return true;
            if (cp >= 0xA0 && cp <= 0x24F) return cp != 0xAD;  // sem hifen suave
            if (cp >= 0x2010 && cp <= 0x2027) return true;      // travessoes, aspas, reticencias
            if (cp == 0x20AC) return true;                      // euro
            return false;
        }

        void anexar(std::string& saida, uint32_t cp) {
            if (cp < 0x80) {
                saida += static_cast<char>(cp);
            } else if (cp < 0x800) {
                saida += static_cast<char>(0xC0 | (cp >> 6));
                saida += static_cast<char>(0x80 | (cp & 0x3F));
            } else {
                saida += static_cast<char>(0xE0 | (cp >> 12));
                saida += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                saida += static_cast<char>(0x80 | (cp & 0x3F));
            }
        }

    } // namespace

    bool lerProjetos(std::string_view json, ListaProjetos& saida) {
        if (json.empty() || json.size() > kMaxBytesProjetos) return false;
        ListaProjetos lida;
        if (glz::read<kLeitura>(lida, json)) return false;
        if (lida.projetos.size() > kMaxProjetos) lida.projetos.resize(kMaxProjetos);
        saida = std::move(lida);
        return true;
    }

    std::string limparTitulo(std::string_view titulo) {
        std::string saida;
        bool espacoPendente = false;
        while (!titulo.empty()) {
            const uint32_t cp = proximo(titulo);
            if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r' || cp == 0xA0) {
                espacoPendente = !saida.empty();
                continue;
            }
            if (!glyphSuportado(cp)) continue;
            if (espacoPendente) saida += ' ';
            espacoPendente = false;
            anexar(saida, cp);
        }
        return saida;
    }

    std::string serializarEstado(const Estado& estado) {
        std::string saida;
        if (glz::write_json(estado, saida)) return {};
        return saida;
    }

    bool lerEstado(std::string_view json, Estado& saida) {
        if (json.empty() || json.size() > kMaxBytesEstado) return false;
        Estado lido;
        if (glz::read<kLeitura>(lido, json)) return false;
        saida = std::move(lido);
        return true;
    }

    std::string textoData(const Data& data) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", data.ano, data.mes, data.dia);
        return buf;
    }

    std::string caminhoLog(const Data& data) {
        return std::string{kPastaLog} + "/" + textoData(data) + ".jsonl";
    }

    std::string linhaLog(const EventoLog& evento) {
        std::string saida;
        if (glz::write_json(evento, saida)) return {};
        saida += '\n';
        return saida;
    }

} // namespace tempo
