#ifndef TEXT_PAGINATOR_H
#define TEXT_PAGINATOR_H

#include <Arduino.h>
#include "config.h"

enum PaginatorAction {
    ACT_NONE = 0,
    ACT_NEXT_PAGE,
    ACT_PREV_PAGE,
    ACT_DISMISS
};

struct PageSpan {
    uint32_t startOffset;
    uint32_t length;
    uint16_t wordCount;
    uint16_t lineCount;
};

class TextPaginator {
public:
    static const int MAX_PAGES = 32;

    TextPaginator();

    void setGeometry(int screenW = 200, int screenH = 200, int marginX = 8, int topMargin = 22, int bottomMargin = 20);
    bool setText(const char *rawText, int fontScale = 1);

    bool nextPage();
    bool prevPage();
    void goToPage(int page);

    int getCurrentPage() const { return currentPage; }
    int getTotalPages()  const { return totalPages; }
    int getFontScale()   const { return fontScale; }
    bool isLastPage()    const { return currentPage >= totalPages - 1; }
    bool isFirstPage()   const { return currentPage == 0; }

    void render(uint8_t *buffer, const char *title = nullptr);

    void setWordsPerMinute(uint16_t wpm) { this->wpm = wpm; }
    void resetPageTimer();
    bool checkAutoAdvance(uint32_t nowMs);

    PaginatorAction handleButton(bool buttonPressed, uint32_t nowMs);

    bool shouldDoFullRefresh() const;
    void notifyRefreshExecuted(bool wasFull);

private:
    String ownedText;
    const char *textPtr;
    size_t textLen;
    int fontScale;
    int charWidth;
    int lineHeight;
    int maxCharsPerLine;
    int maxLinesPerPage;

    int screenWidth;
    int screenHeight;
    int marginX;
    int topMargin;
    int bottomMargin;

    PageSpan pages[MAX_PAGES];
    int totalPages;
    int currentPage;

    uint16_t wpm;
    uint32_t pageStartTimeMs;
    uint32_t targetPageDurationMs;

    int partialUpdatesCount;
    static const int MAX_PARTIAL_BEFORE_FULL = 4;

    bool lastBtnState;
    uint32_t btnPressStartMs;
    uint32_t lastClickReleaseMs;
    uint8_t clickCount;

    int calculateNextWord(const char *p, int maxLen, int &visualLength);
    void computePages();
    uint32_t computePageReadingTimeMs(const PageSpan &span);

    void drawString(uint8_t *buf, int x, int y, const char *str, int len, uint8_t color, int scale);
    void drawFooter(uint8_t *buf);
    void drawHeader(uint8_t *buf, const char *title);
};

#endif // TEXT_PAGINATOR_H
