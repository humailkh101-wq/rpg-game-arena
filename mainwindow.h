#pragma once
#include <QMainWindow>
#include <QWidget>
#include <QTimer>
#include <QElapsedTimer>
#include <QVector>
#include <QString>
#include <QColor>
#include <QPointF>
#include <QLineF>

class QPushButton;
class QTextEdit;
class QLineEdit;
class QStackedWidget;
class QPainter;

struct Character {
    QString name;
    int health;
    int maxHealth;
    int minDamage;
    int maxDamage;
};

struct Blood     { double x, y, vx, vy, life, size; };
struct Stain     { double x, y, r; };
struct Particle  { double x, y, vx, vy, life; };
struct FloatText { QString text; QColor color; double x, y, t, scale; };
struct Fog       { double x, y, r, speed; };

// ---------- The animated cinematic scene ----------
class ArenaWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ArenaWidget(QWidget *parent = nullptr);

    void reset();
    void setNames(const QString &player, const QString &monster);
    void setHealth(int ph, int pmax, int mh, int mmax, bool snap = false);

    void heroAttack();
    void goblinAttack();
    void heal();
    void showDamage(bool onGoblin, int dmg, bool crit);
    void showHeal(int amount);
    void setDefeated(bool heroDead, bool goblinDead);
    void showBanner(const QString &text, const QString &sub, const QColor &color);
    void showTitle(const QString &text);

protected:
    void paintEvent(QPaintEvent *) override;

private slots:
    void tick();

private:
    void spawnBlood(double x, double y, int n, double dir, double power);
    void drawBackground(QPainter &p);
    void drawSkull(QPainter &p, double x, double y, double s);
    void drawStainsAndPools(QPainter &p);
    void drawGoblin(QPainter &p);
    void drawHero(QPainter &p);
    void drawPrince(QPainter &p);
    void drawEffects(QPainter &p);
    void drawBloodDrops(QPainter &p);
    void drawFog(QPainter &p);
    void drawBar(QPainter &p, double x, double y, double w, const QString &name,
                 double shown, int real, int maxv,
                 const QColor &c1, const QColor &c2, bool right);

    QTimer m_timer;
    QElapsedTimer m_clock;
    double m_time = 0;
    double m_lastMs = 0;

    // effect progress: 0..1 while playing, -1 when idle
    double m_heroAtk = -1, m_gobAtk = -1, m_heroHit = -1, m_gobHit = -1;
    double m_slash = -1, m_heal = -1;

    bool m_heroDead = false, m_gobDead = false;
    double m_heroDeadT = 0, m_gobDeadT = 0;
    double m_heroPool = 0, m_gobPool = 0;

    // cinematic effects
    double m_shake = 0, m_flash = 0, m_hurt = 0, m_screenBlood = 0, m_punch = 0;
    QColor m_flashColor = Qt::white;

    QString m_pName, m_mName;
    int m_pHp = 100, m_pMax = 100, m_mHp = 100, m_mMax = 100;
    double m_pShown = 100, m_mShown = 100;

    QVector<QPointF> m_stars;
    QVector<QPointF> m_splats;
    QVector<QLineF> m_cracks;
    QVector<Fog> m_fog;
    QVector<Blood> m_blood;
    QVector<Stain> m_stains;
    QVector<Particle> m_particles;
    QVector<Particle> m_embers;
    QVector<FloatText> m_floats;

    QString m_title;
    double m_titleT = 1;

    QString m_banner, m_bannerSub;
    QColor m_bannerColor;
    double m_bannerT = 0;
};

// ---------- Main window ----------
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void startGame();
    void onAttack();
    void onHeal();
    void onRun();
    void backToStart();
    void tryAttack();
    void tryHeal();
    void tryLeave();

private:
    void monsterTurn();
    bool alive(int id) const { return id == m_session && !m_over; }
    void updateUi(bool snap = false);
    void log(const QString &html);
    void endBattle(const QString &message, const QString &color, const QString &banner);
    void setActionsEnabled(bool on);

    Character m_player;
    Character m_monster;
    int m_potions = 3;
    int m_session = 0;
    bool m_over = true;

    QStackedWidget *m_pages;
    QLineEdit *m_nameEdit;
    ArenaWidget *m_arena;
    QTextEdit *m_log;
    QPushButton *m_attackBtn;
    QPushButton *m_healBtn;
    QPushButton *m_runBtn;
    QPushButton *m_againBtn;
};