#include "TranscriptRemap.h"

namespace TranscriptRemap {

double mapTime(double t, const QVector<TimeRange> &cuts)
{
    double removed = 0.0;
    for (const TimeRange &c : cuts) {
        if (t >= c.end) removed += c.length();
        else if (t > c.start) return c.start - removed;
        else break;
    }
    return t - removed;
}

QVector<TranscriptWord> remapWords(const QVector<TranscriptWord> &words,
                                   const QVector<TimeRange> &cuts)
{
    QVector<TranscriptWord> out;
    out.reserve(words.size());
    for (const TranscriptWord &w : words) {
        const double mid = 0.5 * (w.startSec + w.endSec);
        bool inside = false;
        for (const TimeRange &c : cuts)
            if (mid > c.start && mid < c.end) { inside = true; break; }
        if (inside) continue;
        TranscriptWord n = w;
        n.startSec = mapTime(w.startSec, cuts);
        n.endSec = qMax(n.startSec, mapTime(w.endSec, cuts));
        out.append(n);
    }
    return out;
}

QVector<Sentence> sentencesFromWords(const QVector<TranscriptWord> &words, double gapSec)
{
    QVector<Sentence> out;
    Sentence cur{0, 0, QString()};
    bool open = false;
    for (int i = 0; i < words.size(); ++i) {
        const TranscriptWord &w = words[i];
        if (!open) { cur = {w.startSec, w.endSec, w.text.trimmed()}; open = true; }
        else { cur.endSec = w.endSec; cur.text += QLatin1Char(' ') + w.text.trimmed(); }
        const QString t = w.text.trimmed();
        const bool endsSentence = !t.isEmpty()
            && QStringLiteral(".?!…").contains(t.at(t.size() - 1));
        const bool gap = i + 1 < words.size() && words[i + 1].startSec - w.endSec > gapSec;
        if (endsSentence || gap || i + 1 == words.size()) { out.append(cur); open = false; }
    }
    return out;
}

} // namespace TranscriptRemap
