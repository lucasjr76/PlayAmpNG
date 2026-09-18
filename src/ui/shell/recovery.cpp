#include "ui/shell/recovery.h"

#include <algorithm>

namespace pang::ui::shell {

bool is_reachable(const QRect& window, const QVector<QRect>& screens) {
    if (window.isEmpty() || screens.isEmpty()) return false;

    const QRect strip(window.x(), window.y(), window.width(),
                      std::min(kDragStripHeight, window.height()));
    for (const QRect& screen : screens) {
        const QRect visible = strip.intersected(screen);
        if (!visible.isEmpty() && visible.width() >= kMinimumVisibleWidth) return true;
    }
    return false;
}

QRect recover(const QRect& window, const QVector<QRect>& screens, const QRect& primary) {
    if (is_reachable(window, screens)) return window;
    return QRect(primary.x() + 32, primary.y() + 32, window.width(), window.height());
}

}  // namespace pang::ui::shell
