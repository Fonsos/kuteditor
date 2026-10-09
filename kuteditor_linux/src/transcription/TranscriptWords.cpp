#include "TranscriptWords.h"

#include <QJsonObject>
#include <QVariantMap>
#include <algorithm>

namespace {

bool isOpeningMark(QChar c)
{
    static const QString marks = QStringLiteral("¿¡«“‘([{\"'");
    return marks.contains(c);
}

/// ¿El texto son solo signos de puntuación/símbolos (sin letras ni números)?
bool isPunctuationOnly(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty()) return false;
    for (const QChar c : t)
        if (c.isLetterOrNumber()) return false;
    return true;
}

bool isOpeningOnly(const QString &s)
{
    const QString t = s.trimmed();
    if (t.isEmpty()) return false;
    for (const QChar c : t)
        if (!isOpeningMark(c)) return false;
    return true;
}

} // namespace

namespace TranscriptWords {

QVector<TranscriptWord> fromTokens(const QVector<RawToken> &tokens)
{
    QVector<TranscriptWord> out;
    TranscriptWord cur;
    bool open = false;          // hay una palabra en construcción
    int nTok = 0;
    double pSum = 0.0;
    QString pendingPrefix;      // «¿» que esperan a la siguiente palabra

    auto flush = [&]() {
        if (!open) return;
        cur.prob = nTok > 0 ? float(pSum / nTok) : 0.0f;
        if (!cur.text.trimmed().isEmpty()) out.append(cur);
        cur = TranscriptWord();
        open = false; nTok = 0; pSum = 0.0;
    };

    for (const RawToken &t : tokens) {
        if (t.special) continue;
        if (t.text.isEmpty()) continue;

        const bool startsWord = t.text.at(0).isSpace();
        const bool punct = isPunctuationOnly(t.text);

        if (punct && isOpeningOnly(t.text)) {          // «¿» → prefijo de la siguiente palabra
            flush();
            pendingPrefix += t.text.trimmed();
            continue;
        }
        if (punct) {                                    // «,» «.» «?» → se pegan a la palabra anterior
            if (open) { cur.text += t.text.trimmed(); }
            else if (!out.isEmpty()) { out.last().text += t.text.trimmed(); }
            continue;                                   // su tiempo no cuenta
        }

        if (startsWord || !open) {
            flush();
            cur.text = pendingPrefix + t.text.trimmed();
            pendingPrefix.clear();
            cur.startSec = t.t0;
            cur.endSec = t.t1;
            open = true;
        } else {
            cur.text += t.text;                         // continuación de la palabra («mun» + «do»)
            cur.endSec = std::max(cur.endSec, t.t1);
        }
        ++nTok;
        pSum += t.p;
    }
    flush();
    if (!pendingPrefix.isEmpty() && !out.isEmpty()) out.last().text += pendingPrefix;
    return out;
}

void sanitize(QVector<TranscriptWord> &words, double maxEnd)
{
    constexpr double kMinDur = 0.04;
    double prevEnd = 0.0;
    for (int i = 0; i < words.size(); ++i) {
        TranscriptWord &w = words[i];
        if (w.startSec < prevEnd) w.startSec = prevEnd;         // sin solapes
        if (maxEnd > 0 && w.startSec > maxEnd - kMinDur) w.startSec = std::max(0.0, maxEnd - kMinDur);
        if (w.endSec < w.startSec + kMinDur) w.endSec = w.startSec + kMinDur;
        if (maxEnd > 0 && w.endSec > maxEnd) w.endSec = maxEnd;
        // El final no puede pisar el inicio de la siguiente (si esta empieza después)
        if (i + 1 < words.size()) {
            const double nextStart = words[i + 1].startSec;
            if (nextStart > w.startSec + kMinDur && w.endSec > nextStart) w.endSec = nextStart;
        }
        prevEnd = w.endSec;
    }
}

QJsonArray toJson(const QVector<TranscriptWord> &words)
{
    QJsonArray arr;
    for (const TranscriptWord &w : words) {
        QJsonObject o;
        o["startSec"] = w.startSec;
        o["endSec"]   = w.endSec;
        o["text"]     = w.text;
        o["prob"]     = double(w.prob);
        arr.append(o);
    }
    return arr;
}

QVector<TranscriptWord> fromJson(const QJsonArray &arr)
{
    QVector<TranscriptWord> out;
    out.reserve(arr.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        TranscriptWord w;
        w.startSec = o["startSec"].toDouble();
        w.endSec   = o["endSec"].toDouble();
        w.text     = o["text"].toString();
        w.prob     = float(o["prob"].toDouble());
        out.append(w);
    }
    return out;
}

QVariantList toVariantList(const QVector<TranscriptWord> &words)
{
    QVariantList list;
    list.reserve(words.size());
    for (const TranscriptWord &w : words) {
        QVariantMap m;
        m["startSec"] = w.startSec;
        m["endSec"]   = w.endSec;
        m["text"]     = w.text;
        m["prob"]     = w.prob;
        list.append(m);
    }
    return list;
}

} // namespace TranscriptWords
