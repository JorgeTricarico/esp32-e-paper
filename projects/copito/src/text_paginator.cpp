#include "text_paginator.h"
#include "faces_bitmaps.h"

TextPaginator::TextPaginator() {
    textPtr = nullptr;
    textLen = 0;
    fontScale = 1;
    charWidth = 6;
    lineHeight = 11;
    maxCharsPerLine = 30;
    maxLinesPerPage = 14;

    screenWidth = EPD_WIDTH;
    screenHeight = EPD_HEIGHT;
    marginX = 8;
    topMargin = 22;
    bottomMargin = 20;

    totalPages = 0;
    currentPage = 0;
    wpm = 190;
    pageStartTimeMs = 0;
    targetPageDurationMs = 5000;
    partialUpdatesCount = 0;

    lastBtnState = false;
    btnPressStartMs = 0;
    lastClickReleaseMs = 0;
    clickCount = 0;
}

void TextPaginator::setGeometry(int screenW, int screenH, int mX, int topM, int bottomM) {
    screenWidth = screenW;
    screenHeight = screenH;
    marginX = mX;
    topMargin = topM;
    bottomMargin = bottomM;

    int usableWidth  = screenWidth - (2 * marginX);
    int usableHeight = screenHeight - (topMargin + bottomMargin);

    charWidth = (fontScale == 2) ? 12 : 6;
    lineHeight = (fontScale == 2) ? 20 : 11;

    maxCharsPerLine = usableWidth / charWidth;
    maxLinesPerPage = usableHeight / lineHeight;
}

int TextPaginator::calculateNextWord(const char *p, int remainingLen, int &visualLength) {
    int byteIdx = 0;
    visualLength = 0;

    while (byteIdx < remainingLen && p[byteIdx] != ' ' && p[byteIdx] != '\n' && p[byteIdx] != '\r') {
        uint8_t b1 = (uint8_t)p[byteIdx];
        if (b1 < 0x80) {
            byteIdx++;
        } else if ((b1 & 0xE0) == 0xC0 && byteIdx + 1 < remainingLen) {
            byteIdx += 2;
        } else if ((b1 & 0xF0) == 0xE0 && byteIdx + 2 < remainingLen) {
            byteIdx += 3;
        } else {
            byteIdx++;
        }
        visualLength++;
    }
    return byteIdx;
}

void TextPaginator::computePages() {
    totalPages = 0;
    currentPage = 0;
    if (!textPtr || textLen == 0) return;

    setGeometry(screenWidth, screenHeight, marginX, topMargin, bottomMargin);

    uint32_t cursor = 0;
    while (cursor < textLen && totalPages < MAX_PAGES) {
        PageSpan &span = pages[totalPages];
        span.startOffset = cursor;
        span.wordCount = 0;
        span.lineCount = 0;

        int currentLineVisualChars = 0;

        while (cursor < textLen && span.lineCount < maxLinesPerPage) {
            while (cursor < textLen && textPtr[cursor] == ' ' && currentLineVisualChars == 0) {
                cursor++;
            }
            if (cursor >= textLen) break;

            if (textPtr[cursor] == '\n') {
                cursor++;
                span.lineCount++;
                currentLineVisualChars = 0;
                continue;
            }
            if (textPtr[cursor] == '\r') {
                cursor++;
                continue;
            }

            int wordVisualLen = 0;
            int wordByteLen = calculateNextWord(&textPtr[cursor], textLen - cursor, wordVisualLen);

            if (currentLineVisualChars + wordVisualLen > maxCharsPerLine && currentLineVisualChars > 0) {
                span.lineCount++;
                currentLineVisualChars = 0;
                if (span.lineCount >= maxLinesPerPage) {
                    break;
                }
            }

            if (wordVisualLen > maxCharsPerLine) {
                wordByteLen = maxCharsPerLine;
                wordVisualLen = maxCharsPerLine;
            }

            cursor += wordByteLen;
            currentLineVisualChars += wordVisualLen;
            span.wordCount++;

            if (cursor < textLen && textPtr[cursor] == ' ') {
                cursor++;
                currentLineVisualChars++;
            }
        }

        span.length = cursor - span.startOffset;
        totalPages++;
    }

    resetPageTimer();
}

bool TextPaginator::setText(const char *rawText, int scale) {
    if (!rawText) return false;
    ownedText = rawText;
    textPtr = ownedText.c_str();
    textLen = ownedText.length();
    fontScale = scale;
    computePages();
    return totalPages > 0;
}

uint32_t TextPaginator::computePageReadingTimeMs(const PageSpan &span) {
    uint32_t ms = 1400 + (uint32_t)((span.wordCount * 60000UL) / (wpm > 0 ? wpm : 190));
    if (ms < 3000) ms = 3000;
    if (ms > 35000) ms = 35000;
    return ms;
}

void TextPaginator::resetPageTimer() {
    pageStartTimeMs = millis();
    if (currentPage >= 0 && currentPage < totalPages) {
        targetPageDurationMs = computePageReadingTimeMs(pages[currentPage]);
    } else {
        targetPageDurationMs = 5000;
    }
}

bool TextPaginator::nextPage() {
    if (currentPage < totalPages - 1) {
        currentPage++;
        resetPageTimer();
        return true;
    }
    return false;
}

bool TextPaginator::prevPage() {
    if (currentPage > 0) {
        currentPage--;
        resetPageTimer();
        return true;
    }
    return false;
}

void TextPaginator::goToPage(int page) {
    if (page >= 0 && page < totalPages) {
        currentPage = page;
        resetPageTimer();
    }
}

bool TextPaginator::checkAutoAdvance(uint32_t nowMs) {
    if (totalPages <= 1) return false;
    if (nowMs - pageStartTimeMs >= targetPageDurationMs) {
        return nextPage();
    }
    return false;
}

bool TextPaginator::shouldDoFullRefresh() const {
    return (currentPage == 0 && partialUpdatesCount == 0) || 
           (partialUpdatesCount >= MAX_PARTIAL_BEFORE_FULL);
}

void TextPaginator::notifyRefreshExecuted(bool wasFull) {
    if (wasFull) {
        partialUpdatesCount = 0;
    } else {
        partialUpdatesCount++;
    }
}

PaginatorAction TextPaginator::handleButton(bool buttonPressed, uint32_t nowMs) {
    PaginatorAction action = ACT_NONE;

    if (buttonPressed && !lastBtnState) {
        btnPressStartMs = nowMs;
        lastBtnState = true;
    } else if (!buttonPressed && lastBtnState) {
        lastBtnState = false;
        uint32_t pressDuration = nowMs - btnPressStartMs;

        if (pressDuration >= 1200) {
            clickCount = 0;
            return ACT_DISMISS;
        } else if (pressDuration >= 50) {
            clickCount++;
            lastClickReleaseMs = nowMs;
        }
    }

    if (clickCount > 0 && !lastBtnState && (nowMs - lastClickReleaseMs > 280)) {
        if (clickCount == 1) {
            if (isLastPage()) {
                action = ACT_DISMISS;
            } else {
                action = ACT_NEXT_PAGE;
            }
        } else if (clickCount >= 2) {
            action = ACT_PREV_PAGE;
        }
        clickCount = 0;
    }

    return action;
}

void TextPaginator::drawString(uint8_t *buf, int x, int y, const char *str, int len, uint8_t color, int scale) {
    int curX = x;
    int i = 0;
    while (i < len && str[i]) {
        uint8_t b1 = (uint8_t)str[i++];
        char c = ' ';
        if (b1 < 0x80) {
            c = (char)b1;
        } else if (b1 == 0xC3 && i < len) {
            uint8_t b2 = (uint8_t)str[i++];
            switch (b2) {
                case 0xA1: case 0x81: c = 'A'; break;
                case 0xA9: case 0x89: c = 'E'; break;
                case 0xAD: case 0x8D: c = 'I'; break;
                case 0xB3: case 0x93: c = 'O'; break;
                case 0xBA: case 0x9A: c = 'U'; break;
                case 0xBC: case 0x9C: c = 'U'; break;
                case 0xB1: case 0x91: c = 'N'; break;
                default: c = ' '; break;
            }
        } else if (b1 == 0xC2 && i < len) {
            uint8_t b2 = (uint8_t)str[i++];
            if (b2 == 0xBF) c = '?';
            else if (b2 == 0xA1) c = '!';
            else c = ' ';
        }
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        FaceRenderer::drawChar5x7(buf, curX, y, c, color, scale);
        curX += (scale == 2) ? 12 : 6;
    }
}

void TextPaginator::drawHeader(uint8_t *buf, const char *title) {
    FaceRenderer::drawTopBar(buf);
    if (title && strlen(title) > 0) {
        drawString(buf, 48, 8, title, strlen(title), 0, 1);
    }
    FaceRenderer::drawLine(buf, 8, 20, screenWidth - 8, 20, 0, 1);
}

void TextPaginator::drawFooter(uint8_t *buf) {
    int footerY = screenHeight - bottomMargin + 2;
    FaceRenderer::drawLine(buf, 8, footerY, screenWidth - 8, footerY, 0, 1);

    char pageBuf[16];
    snprintf(pageBuf, sizeof(pageBuf), "PAG %d/%d", currentPage + 1, totalPages);
    drawString(buf, 10, footerY + 5, pageBuf, strlen(pageBuf), 0, 1);

    int totalDots = (totalPages > 6) ? 6 : totalPages;
    int startDotX = 90 - (totalDots * 4);
    for (int d = 0; d < totalDots; d++) {
        int dotX = startDotX + (d * 8);
        if (d == currentPage) {
            FaceRenderer::drawCircle(buf, dotX, footerY + 8, 2, 0, true);
        } else {
            FaceRenderer::drawCircle(buf, dotX, footerY + 8, 2, 0, false);
        }
    }

    if (isLastPage()) {
        drawString(buf, 116, footerY + 5, "[BOOT:HABLAR]", 13, 0, 1);
    } else {
        drawString(buf, 116, footerY + 5, "[BOOT:LEER->]", 13, 0, 1);
    }
}

void TextPaginator::render(uint8_t *buf, const char *title) {
    if (!buf || totalPages == 0) return;

    FaceRenderer::clearBuffer(buf, 0xFF);
    drawHeader(buf, title);
    drawFooter(buf);

    const PageSpan &span = pages[currentPage];
    const char *p = &textPtr[span.startOffset];
    int remaining = span.length;

    int cursorY = topMargin + 3;
    int lineVisualChars = 0;
    int charW = (fontScale == 2) ? 12 : 6;

    while (remaining > 0 && cursorY < screenHeight - bottomMargin) {
        while (remaining > 0 && *p == ' ' && lineVisualChars == 0) {
            p++; remaining--;
        }
        if (remaining <= 0) break;

        if (*p == '\n') {
            p++; remaining--;
            cursorY += lineHeight;
            lineVisualChars = 0;
            continue;
        }

        int wordVisualLen = 0;
        int wordByteLen = calculateNextWord(p, remaining, wordVisualLen);

        if (lineVisualChars + wordVisualLen > maxCharsPerLine && lineVisualChars > 0) {
            cursorY += lineHeight;
            lineVisualChars = 0;
            if (cursorY >= screenHeight - bottomMargin) break;
        }

        drawString(buf, marginX + (lineVisualChars * charW), cursorY, p, wordByteLen, 0, fontScale);
        p += wordByteLen;
        remaining -= wordByteLen;
        lineVisualChars += wordVisualLen;

        if (remaining > 0 && *p == ' ') {
            p++; remaining--;
            lineVisualChars++;
        }
    }
}
