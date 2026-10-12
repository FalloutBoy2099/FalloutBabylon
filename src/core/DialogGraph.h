#pragma once

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

// What a decompiled Fallout script (SSL, as produced by int2ssl) says about its dialogue:
// for every procedure the NPC lines, the player options and the procedures it calls, in source order.
struct DialogEvent {
    enum Kind { Reply, Message, Option, Call };
    Kind kind = Call;
    int msgId = -1;        // line in the dialogue .msg file (-1 when it is not a literal number)
    QString target;        // procedure called / chosen by an option
    int iq = 0;            // IQ condition of an option (giq_option: <0 low IQ only, >0 minimum IQ)
};

struct DialogProc {
    QString name;
    QVector<DialogEvent> events;
    bool hasDialogue() const
    {
        for (const DialogEvent &e : events)
            if (e.kind != DialogEvent::Call)
                return true;
        return false;
    }
};

// A node of the dialogue tree shown to the translator.
struct DialogNode {
    enum Type { Npc, Message, Player, Call, Reference };
    Type type = Npc;
    int msgId = -1;
    QString proc;          // Call/Reference: the procedure; others: the procedure the line belongs to
    int iq = 0;
    QVector<DialogNode> children;
};

class DialogGraph
{
public:
    static DialogGraph parseSsl(const QString &ssl);

    bool isEmpty() const { return m_order.isEmpty(); }
    bool hasDialogue() const;
    const DialogProc *proc(const QString &name) const;

    // Procedures that start a conversation: talk_p_proc, plus unreferenced procedures holding dialogue.
    QStringList roots() const;
    // The conversation as a tree. A procedure reached again is shown as a Reference node.
    QVector<DialogNode> buildTree() const;

    // Every message id used by a Reply/Message/Option, in order of first appearance.
    QVector<int> usedMessageIds() const;

private:
    void expand(const QString &name, QVector<DialogNode> &out, QSet<QString> &expanded) const;

    QHash<QString, DialogProc> m_procs;
    QStringList m_order;
};
