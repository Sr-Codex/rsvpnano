#include "board/BoardAudio.h"

namespace Board::Audio {

    bool begin() {
        return false;
    }

    bool beep() {
        return false;
    }

    bool play(const som::Pedido&) {
        return false;
    }

    void waitOrStop(uint32_t) {}

    bool available() {
        return false;
    }

} // namespace Board::Audio
