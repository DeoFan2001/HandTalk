#include "GestureEngine.h"
#include "Alphabet.h"

namespace HandTracker {

    void GestureEngine::reset() {
        seg_.reset();
        dynLock_ = false;
        noHand_ = 0;
        lastCode_.clear();
        lastHand_ = false;
    }

    FrameOut GestureEngine::update(const HandPoints* pts, bool isLeft, bool commit, bool fresh) {
        FrameOut o;

        // === КАДР-ПОВТОР (режим экономии): новых точек нет, считаем только время для текста ===
        if (!fresh) {
            o.moving = seg_.moving();
            if (commit) text_.update(lastCode_, lastHand_);
            return o;
        }

        // === ДВИЖЕНИЕ ===
        SeqFeatures seq;
        bool ended = seg_.update(pts, isLeft, seq);
        o.moving = seg_.moving();

        if (ended) {
            o.hasSeq = true;
            o.seq = seq;
            dynLock_ = false;                              // новое движение снимает блокировку
            if (commit && dyn_ && dyn_->ready()) {
                GestureResult r = dyn_->classify(seq);
                if (r.valid && r.confidence >= p_.dynMinConfidence) {
                    wchar_t ch = letterFromCode(r.code);
                    if (ch) {
                        o.dynCode = r.code;
                        o.dynConf = r.confidence;
                        text_.append(ch);
                        dynLock_ = true;
                    }
                }
            }
        }

        // === РУКИ НЕТ ===
        if (!pts) {
            if (++noHand_ >= p_.unlockFrames) dynLock_ = false;
            lastCode_.clear();
            lastHand_ = false;
            if (commit) text_.update("", false);
            return o;
        }
        noHand_ = 0;
        lastHand_ = true;

        // === ПОЗА (только когда рука стоит) ===
        if (!o.moving) {
            Features f;
            if (normalizeHand(*pts, isLeft, f)) {
                o.hasFeatures = true;
                o.feat = f;
                if (stat_ && stat_->ready()) {
                    GestureResult r = stat_->classify(f);
                    if (r.valid && r.confidence >= p_.minConfidence) { o.code = r.code; }
                }
            }
        }

        // пока рука движется или идёт блокировка после буквы с движением, обычные буквы не вводим
        lastCode_ = (!o.moving && !dynLock_) ? o.code : std::string();
        if (commit) text_.update(lastCode_, true);
        return o;
    }

} // namespace HandTracker
