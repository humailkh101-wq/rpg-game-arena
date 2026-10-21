#include "mainwindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <QShortcut>
#include <QKeySequence>
#include <QStackedWidget>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QtMath>
#include <cmath>

static const double PI = 3.14159265358979;
static const double W = 800, H = 440, GY = 370;   // scene size and ground line
static const double HX = 170, GX = 610;           // hero / goblin x position

static double rnd(double a, double b)
{
    return a + QRandomGenerator::global()->generateDouble() * (b - a);
}
static double lerp(double a, double b, double t) { return a + (b - a) * t; }

static QColor mixColor(const QColor &a, const QColor &b, double t)
{
    t = qBound(0.0, t, 1.0);
    return QColor(int(lerp(a.red(), b.red(), t)),
                  int(lerp(a.green(), b.green(), t)),
                  int(lerp(a.blue(), b.blue(), t)),
                  a.alpha());
}
static QColor alphaOf(QColor c, int a) { c.setAlpha(qBound(0, a, 255)); return c; }

// arm angle during a swing: windup -> strike -> return
static double swing(double t, double rest, double wind, double strike)
{
    if (t < 0.35) return lerp(rest, wind, t / 0.35);
    if (t < 0.55) return lerp(wind, strike, (t - 0.35) / 0.2);
    return lerp(strike, rest, (t - 0.55) / 0.45);
}

// =====================================================================
//                            ArenaWidget
// =====================================================================
ArenaWidget::ArenaWidget(QWidget *parent) : QWidget(parent)
{
    setMinimumSize(560, 330);

    for (int i = 0; i < 70; ++i)
        m_stars.append(QPointF(rnd(0, W), rnd(24, 230)));
    for (int i = 0; i < 6; ++i)
        m_fog.append({rnd(0, W), GY + rnd(-10, 40), rnd(120, 220), rnd(6, 18) * (i % 2 ? 1 : -1)});
    for (int i = 0; i < 45; ++i) {
        double x = rnd(0, W), y = rnd(GY - 20, H);
        m_cracks.append(QLineF(x, y, x + rnd(-45, 45), y + rnd(-7, 10)));
    }
    for (int i = 0; i < 14; ++i)
        m_splats.append(QPointF(rnd(0, W), rnd(26, 150)));

    connect(&m_timer, &QTimer::timeout, this, &ArenaWidget::tick);
    m_timer.start(16);
    m_clock.start();
}

void ArenaWidget::reset()
{
    m_heroAtk = m_gobAtk = m_heroHit = m_gobHit = m_slash = m_heal = -1;
    m_heroDead = m_gobDead = false;
    m_heroDeadT = m_gobDeadT = 0;
    m_heroPool = m_gobPool = 0;
    m_shake = m_flash = m_hurt = m_screenBlood = m_punch = 0;
    m_blood.clear();
    m_stains.clear();
    m_particles.clear();
    m_floats.clear();
    m_banner.clear();
    m_bannerT = 0;
}

void ArenaWidget::setNames(const QString &player, const QString &monster)
{
    m_pName = player;
    m_mName = monster;
}

void ArenaWidget::setHealth(int ph, int pmax, int mh, int mmax, bool snap)
{
    m_pHp = ph; m_pMax = pmax; m_mHp = mh; m_mMax = mmax;
    if (snap) { m_pShown = ph; m_mShown = mh; }
}

void ArenaWidget::heroAttack()   { m_heroAtk = 0; }
void ArenaWidget::goblinAttack() { m_gobAtk = 0; }
void ArenaWidget::heal()         { m_heal = 0; }

void ArenaWidget::spawnBlood(double x, double y, int n, double dir, double power)
{
    for (int i = 0; i < n; ++i) {
        m_blood.append({x + rnd(-6, 6), y + rnd(-6, 6),
                        dir * rnd(60, 420) * power + rnd(-70, 70),
                        -rnd(80, 480) * power,
                        1.6, rnd(1.5, 4.8)});
    }
}

void ArenaWidget::showDamage(bool onGoblin, int dmg, bool crit)
{
    double power = crit ? 1.5 : 1.0;
    m_shake = qMax(m_shake, 5.0 + dmg * 0.5 + (crit ? 8 : 0));
    m_punch = 1.0;

    if (onGoblin) {
        m_gobHit = 0;
        m_slash = 0;
        m_flash = 0.25; m_flashColor = QColor(255, 255, 255);
        spawnBlood(GX - 45, GY - 150, 30 + dmg * 2, 1.0, power);
        m_floats.append({QString("-%1").arg(dmg), crit ? QColor(255, 210, 60) : QColor(255, 80, 60),
                         GX - 40, GY - 230, 0, crit ? 1.5 : 1.0});
    } else {
        m_heroHit = 0;
        m_flash = 0.4; m_flashColor = QColor(200, 0, 0);
        m_hurt = 1.0;
        m_screenBlood = qMin(1.0, m_screenBlood + 0.55);
        spawnBlood(HX + 25, GY - 95, 30 + dmg * 2, -1.0, power);
        m_floats.append({QString("-%1").arg(dmg), QColor(255, 80, 60),
                         HX, GY - 190, 0, crit ? 1.5 : 1.0});
    }
}

void ArenaWidget::showHeal(int amount)
{
    m_floats.append({QString("+%1").arg(amount), QColor(90, 255, 140), HX, GY - 190, 0, 1.2});
}

void ArenaWidget::setDefeated(bool heroDead, bool goblinDead)
{
    m_heroDead = heroDead;
    m_gobDead = goblinDead;
    m_shake = 24;
    m_punch = 1.0;
    if (goblinDead) spawnBlood(GX - 20, GY - 130, 140, 1.0, 1.7);
    if (heroDead)   spawnBlood(HX, GY - 100, 140, -1.0, 1.7);
}

void ArenaWidget::showBanner(const QString &text, const QString &sub, const QColor &color)
{
    m_banner = text;
    m_bannerSub = sub;
    m_bannerColor = color;
    m_bannerT = 0;
}

void ArenaWidget::showTitle(const QString &text)
{
    m_title = text;
    m_titleT = 0;
}

void ArenaWidget::tick()
{
    double now = double(m_clock.elapsed());
    double dt = qBound(1.0, now - m_lastMs, 50.0);
    m_lastMs = now;
    m_time = now / 1000.0;
    const double ds = dt / 1000.0;

    auto adv = [&](double &v, double dur) {
        if (v >= 0) { v += dt / dur; if (v >= 1) v = -1; }
    };
    adv(m_heroAtk, 700);
    adv(m_gobAtk, 900);
    adv(m_heroHit, 450);
    adv(m_gobHit, 450);
    adv(m_slash, 400);
    adv(m_heal, 1600);

    // healing sparkles around the hero
    if (m_heal >= 0.1 && m_heal < 0.9) {
        for (int i = 0; i < 2; ++i)
            m_particles.append({HX + rnd(-45, 45), GY - rnd(10, 150),
                                rnd(-10, 10), rnd(-120, -60), 1.0});
    }
    for (int i = m_particles.size() - 1; i >= 0; --i) {
        Particle &pt = m_particles[i];
        pt.x += pt.vx * ds;
        pt.y += pt.vy * ds;
        pt.life -= ds * 0.9;
        if (pt.life <= 0) m_particles.removeAt(i);
    }

    // torch embers
    if (QRandomGenerator::global()->generateDouble() < 0.35) {
        double tx = (QRandomGenerator::global()->bounded(2) == 0) ? 41 : 759;
        m_embers.append({tx + rnd(-6, 6), GY - 200, rnd(-14, 14), -rnd(30, 80), 1.0});
    }
    for (int i = m_embers.size() - 1; i >= 0; --i) {
        Particle &e = m_embers[i];
        e.x += (e.vx + 12 * qSin(m_time * 3 + i)) * ds;
        e.y += e.vy * ds;
        e.life -= ds * 0.6;
        if (e.life <= 0) m_embers.removeAt(i);
    }

    // blood drops fall and leave stains on the floor
    for (int i = m_blood.size() - 1; i >= 0; --i) {
        Blood &b = m_blood[i];
        b.vy += 1100 * ds;
        b.x += b.vx * ds;
        b.y += b.vy * ds;
        b.life -= ds;
        double landY = GY + 6 + (int(b.size * 10) % 18);
        if (b.y >= landY && b.vy > 0) {
            m_stains.append({b.x, landY, b.size * rnd(1.2, 2.6)});
            if (m_stains.size() > 420) m_stains.removeFirst();
            m_blood.removeAt(i);
        } else if (b.life <= 0) {
            m_blood.removeAt(i);
        }
    }

    for (int i = m_floats.size() - 1; i >= 0; --i) {
        m_floats[i].t += ds / 1.2;
        if (m_floats[i].t >= 1) m_floats.removeAt(i);
    }

    for (Fog &f : m_fog) {
        f.x += f.speed * ds;
        if (f.x > W + f.r) f.x = -f.r;
        if (f.x < -f.r) f.x = W + f.r;
    }

    if (m_gobDead) {
        if (m_gobDeadT < 1) m_gobDeadT = qMin(1.0, m_gobDeadT + dt / 800.0);
        m_gobPool = qMin(1.0, m_gobPool + ds / 4.0);
    }
    if (m_heroDead) {
        if (m_heroDeadT < 1) m_heroDeadT = qMin(1.0, m_heroDeadT + dt / 800.0);
        m_heroPool = qMin(1.0, m_heroPool + ds / 4.0);
    }
    if (!m_banner.isEmpty() && m_bannerT < 1) m_bannerT = qMin(1.0, m_bannerT + dt / 1200.0);
    if (m_titleT < 1) m_titleT = qMin(1.0, m_titleT + dt / 3200.0);

    m_shake *= qPow(0.9, dt / 16.0);
    m_flash = qMax(0.0, m_flash - ds * 2.5);
    m_hurt = qMax(0.0, m_hurt - ds * 0.9);
    m_screenBlood = qMax(0.0, m_screenBlood - ds * 0.12);
    m_punch = qMax(0.0, m_punch - ds * 3.0);

    m_pShown += (m_pHp - m_pShown) * 0.12;
    m_mShown += (m_mHp - m_mShown) * 0.12;

    update();
}

void ArenaWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::black);

    double s = qMin(width() / W, height() / H);
    p.translate((width() - W * s) / 2, (height() - H * s) / 2);
    p.scale(s, s);
    p.setClipRect(QRectF(0, 0, W, H));

    // ---------- world (with camera shake and zoom) ----------
    p.save();
    double zoom = 1.02 + 0.03 * m_punch + 0.05 * m_bannerT;
    double sx = m_shake > 0.2 ? rnd(-m_shake, m_shake) : 0;
    double sy = m_shake > 0.2 ? rnd(-m_shake, m_shake) : 0;
    p.translate(W / 2 + sx, H / 2 + sy);
    p.scale(zoom, zoom);
    p.translate(-W / 2, -H / 2);

    drawBackground(p);
    drawStainsAndPools(p);
    drawPrince(p);
    drawHero(p);
    drawGoblin(p);
    drawEffects(p);
    drawBloodDrops(p);
    drawFog(p);
    p.restore();

    // ---------- screen overlays ----------
    // dark vignette
    QRadialGradient vg(W / 2, H / 2, W * 0.58);
    vg.setColorAt(0.5, QColor(0, 0, 0, 0));
    vg.setColorAt(1.0, QColor(0, 0, 0, 215));
    p.fillRect(QRectF(0, 0, W, H), vg);

    // red hurt vignette + low health heartbeat
    double lowHp = (m_pMax > 0) ? double(m_pHp) / m_pMax : 1.0;
    double hurt = m_hurt * 0.9;
    if (lowHp < 0.3 && !m_heroDead)
        hurt = qMax(hurt, 0.25 + 0.2 * qSin(m_time * 6.5));
    if (hurt > 0.01) {
        QRadialGradient hg(W / 2, H / 2, W * 0.6);
        hg.setColorAt(0.4, QColor(160, 0, 0, 0));
        hg.setColorAt(1.0, QColor(170, 0, 0, int(210 * qBound(0.0, hurt, 1.0))));
        p.fillRect(QRectF(0, 0, W, H), hg);
    }

    // blood on the "camera lens"
    if (m_screenBlood > 0.02) {
        double a = qBound(0.0, m_screenBlood, 1.0);
        p.setPen(Qt::NoPen);
        for (int i = 0; i < m_splats.size(); ++i) {
            double r = 10 + (i % 4) * 8;
            QPointF c = m_splats[i];
            p.setBrush(QColor(110, 4, 6, int(200 * a)));
            p.drawEllipse(c, r, r * 0.8);
            p.setPen(QPen(QColor(110, 4, 6, int(190 * a)), 4, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c, QPointF(c.x(), c.y() + (25 + (i % 3) * 20) * (1.2 - a * 0.4)));
            p.setPen(Qt::NoPen);
        }
    }

    if (m_flash > 0.01) {
        p.setPen(Qt::NoPen);
        p.fillRect(QRectF(0, 0, W, H), alphaOf(m_flashColor, int(150 * m_flash)));
    }

    // HUD
    drawBar(p, 24, 56, 300, m_pName, m_pShown, m_pHp, m_pMax,
            QColor("#58e07a"), QColor("#1f9d4a"), false);
    drawBar(p, 476, 56, 300, m_mName, m_mShown, m_mHp, m_mMax,
            QColor("#ff5a48"), QColor("#8e1a12"), true);

    // floating damage / heal numbers
    for (const FloatText &f : m_floats) {
        QFont font = p.font();
        font.setPixelSize(int(38 * f.scale));
        font.setBold(true);
        p.setFont(font);
        int al = int(255 * (1 - f.t));
        QRectF r(f.x - 100, f.y - 70 * f.t - 30, 200, 60);
        p.setPen(QColor(0, 0, 0, al));
        p.drawText(r.translated(2, 2), Qt::AlignCenter, f.text);
        p.setPen(alphaOf(f.color, al));
        p.drawText(r, Qt::AlignCenter, f.text);
    }

    // intro title
    if (m_titleT < 1 && !m_title.isEmpty()) {
        double a = qMin(qMin(1.0, m_titleT / 0.2), qMin(1.0, (1 - m_titleT) / 0.3));
        QFont tf("Georgia");
        tf.setPixelSize(34);
        tf.setItalic(true);
        tf.setLetterSpacing(QFont::AbsoluteSpacing, 4);
        p.setFont(tf);
        p.setPen(QColor(0, 0, 0, int(220 * a)));
        p.drawText(QRectF(2, 192, W, 50), Qt::AlignCenter, m_title);
        p.setPen(QColor(235, 215, 190, int(255 * a)));
        p.drawText(QRectF(0, 190, W, 50), Qt::AlignCenter, m_title);
        p.setPen(QPen(QColor(170, 20, 20, int(230 * a)), 2));
        p.drawLine(QPointF(W / 2 - 200 * a, 240), QPointF(W / 2 + 200 * a, 240));
    }

    // victory / defeat banner
    if (!m_banner.isEmpty()) {
        p.setPen(Qt::NoPen);
        p.fillRect(QRectF(0, 0, W, H), QColor(0, 0, 0, int(165 * m_bannerT)));
        QFont bf("Georgia");
        bf.setPixelSize(int(60 + 22 * m_bannerT));
        bf.setBold(true);
        bf.setLetterSpacing(QFont::AbsoluteSpacing, 6 + 6 * m_bannerT);
        p.setFont(bf);
        p.setPen(QColor(0, 0, 0, int(255 * m_bannerT)));
        p.drawText(QRectF(3, 153, W, 100), Qt::AlignCenter, m_banner);
        p.setPen(alphaOf(m_bannerColor, int(255 * m_bannerT)));
        p.drawText(QRectF(0, 150, W, 100), Qt::AlignCenter, m_banner);

        QFont sf("Georgia");
        sf.setPixelSize(20);
        sf.setItalic(true);
        p.setFont(sf);
        p.setPen(QColor(230, 220, 210, int(230 * m_bannerT)));
        p.drawText(QRectF(0, 250, W, 40), Qt::AlignCenter, m_bannerSub);
    }

    // film grain
    for (int i = 0; i < 70; ++i) {
        p.setPen(QPen(QColor(255, 255, 255, int(rnd(4, 16))), 1.5));
        p.drawPoint(QPointF(rnd(0, W), rnd(0, H)));
    }

    // cinematic letterbox
    p.setPen(Qt::NoPen);
    p.fillRect(QRectF(0, 0, W, 22), Qt::black);
    p.fillRect(QRectF(0, H - 22, W, 22), Qt::black);
}

void ArenaWidget::drawSkull(QPainter &p, double x, double y, double s)
{
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#d8d0b6"));
    p.drawEllipse(QPointF(x, y), 12 * s, 13 * s);
    p.drawRect(QRectF(x - 7 * s, y + 6 * s, 14 * s, 9 * s));
    p.setBrush(QColor("#120a0a"));
    p.drawEllipse(QPointF(x - 5 * s, y - 1 * s), 3.4 * s, 4 * s);
    p.drawEllipse(QPointF(x + 5 * s, y - 1 * s), 3.4 * s, 4 * s);
    QPolygonF nose;
    nose << QPointF(x, y + 3 * s) << QPointF(x - 2 * s, y + 8 * s) << QPointF(x + 2 * s, y + 8 * s);
    p.drawPolygon(nose);
    p.setPen(QPen(QColor("#120a0a"), 1.2));
    for (int i = -2; i <= 2; ++i)
        p.drawLine(QPointF(x + i * 2.6 * s, y + 10 * s), QPointF(x + i * 2.6 * s, y + 15 * s));
    p.setPen(QPen(QColor(120, 6, 8), 2.5 * s, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(x - 9 * s, y - 8 * s), QPointF(x - 9 * s, y + 12 * s));
    p.setPen(Qt::NoPen);
}

void ArenaWidget::drawBackground(QPainter &p)
{
    // night sky
    QLinearGradient sky(0, 0, 0, GY);
    sky.setColorAt(0, QColor("#05050d"));
    sky.setColorAt(0.55, QColor("#1c0f2e"));
    sky.setColorAt(1, QColor("#4a1a2a"));
    p.fillRect(QRectF(0, 0, W, H), sky);

    p.setPen(Qt::NoPen);
    for (int i = 0; i < m_stars.size(); ++i) {
        int a = int(130 + 100 * qSin(m_time * 2 + i));
        p.setBrush(QColor(255, 255, 255, qBound(30, a, 255)));
        p.drawEllipse(m_stars[i], 1.5, 1.5);
    }

    // blood moon
    QPointF mc(400, 118);
    QRadialGradient mg(mc, 160);
    mg.setColorAt(0, QColor(220, 50, 40, 130));
    mg.setColorAt(1, QColor(220, 50, 40, 0));
    p.setBrush(mg);
    p.drawEllipse(mc, 160, 160);
    QRadialGradient mb(QPointF(390, 108), 52);
    mb.setColorAt(0, QColor("#ff8a6a"));
    mb.setColorAt(1, QColor("#8e1a18"));
    p.setBrush(mb);
    p.drawEllipse(mc, 46, 46);
    p.setBrush(QColor(110, 15, 15, 110));
    p.drawEllipse(QPointF(384, 106), 10, 10);
    p.drawEllipse(QPointF(412, 126), 7, 7);
    p.drawEllipse(QPointF(402, 98), 5, 5);

    // drifting dark clouds
    for (int i = 0; i < 4; ++i) {
        double cx = std::fmod(m_time * (6 + i * 3) + i * 230, W + 400) - 200;
        p.setBrush(QColor(15, 8, 25, 130));
        p.drawEllipse(QPointF(cx, 80 + i * 28), 170, 20);
    }

    // mountains
    QPolygonF mt;
    mt << QPointF(0, GY - 130) << QPointF(0, GY - 200) << QPointF(90, GY - 250) << QPointF(180, GY - 190)
       << QPointF(300, GY - 260) << QPointF(430, GY - 185) << QPointF(560, GY - 250)
       << QPointF(700, GY - 195) << QPointF(800, GY - 240) << QPointF(800, GY - 130);
    p.setBrush(QColor("#140c22"));
    p.drawPolygon(mt);

    // stone arena wall with arches
    p.setBrush(QColor("#18121f"));
    p.drawRect(QRectF(0, GY - 130, W, 100));
    p.setPen(QPen(QColor(255, 255, 255, 12), 2));
    for (int r = 0; r < 4; ++r)
        p.drawLine(QPointF(0, GY - 130 + r * 25), QPointF(W, GY - 130 + r * 25));
    p.setPen(Qt::NoPen);
    for (int i = 0; i < 6; ++i) {
        double x = 70 + i * 130;
        QPainterPath arch;
        arch.moveTo(x - 35, GY - 30);
        arch.lineTo(x - 35, GY - 85);
        arch.arcTo(QRectF(x - 35, GY - 120, 70, 70), 180, -180);
        arch.lineTo(x + 35, GY - 30);
        arch.closeSubpath();
        p.setBrush(QColor("#08050c"));
        p.drawPath(arch);
    }

    // floor
    QLinearGradient fl(0, GY - 30, 0, H);
    fl.setColorAt(0, QColor("#2b2230"));
    fl.setColorAt(1, QColor("#0d0a12"));
    p.setBrush(fl);
    p.drawRect(QRectF(0, GY - 30, W, H - (GY - 30)));
    p.setPen(QPen(QColor(255, 255, 255, 35), 2));
    p.drawLine(QPointF(0, GY - 30), QPointF(W, GY - 30));
    p.setPen(QPen(QColor(0, 0, 0, 120), 1.5));
    for (const QLineF &l : m_cracks)
        p.drawLine(l);
    p.setPen(Qt::NoPen);

    // skulls on pikes
    for (double px : {85.0, 725.0}) {
        p.setBrush(QColor("#3a2c1c"));
        p.drawRect(QRectF(px - 2.5, GY - 150, 5, 125));
        drawSkull(p, px, GY - 160, 1.0);
    }

    // torches
    for (int x : {41, 759}) {
        p.setBrush(QColor("#2c2438"));
        p.drawRect(QRectF(x - 9, GY - 185, 18, 155));
        p.setBrush(QColor("#3a3048"));
        p.drawRect(QRectF(x - 15, GY - 192, 30, 12));

        double f = 1 + 0.15 * qSin(m_time * 13 + x);
        QRadialGradient fg(QPointF(x, GY - 200), 80 * f);
        fg.setColorAt(0, QColor(255, 160, 50, 160));
        fg.setColorAt(1, QColor(255, 120, 20, 0));
        p.setBrush(fg);
        p.drawEllipse(QPointF(x, GY - 200), 80 * f, 80 * f);

        QRadialGradient lg(QPointF(x, GY - 100), 260);
        lg.setColorAt(0, QColor(255, 140, 40, 45));
        lg.setColorAt(1, QColor(255, 140, 40, 0));
        p.setBrush(lg);
        p.drawEllipse(QPointF(x, GY - 100), 260, 260);

        p.setBrush(QColor(255, 140, 30));
        p.drawEllipse(QPointF(x, GY - 204), 9, 16 * f);
        p.setBrush(QColor(255, 235, 130));
        p.drawEllipse(QPointF(x, GY - 200), 4.5, 9 * f);
    }
}

void ArenaWidget::drawStainsAndPools(QPainter &p)
{
    p.setPen(Qt::NoPen);
    for (const Stain &s : m_stains) {
        p.setBrush(QColor(95, 5, 8, 190));
        p.drawEllipse(QPointF(s.x, s.y), s.r, s.r * 0.35);
    }

    auto pool = [&](double cx, double cy, double rx) {
        if (rx < 2) return;
        p.save();
        p.translate(cx, cy);
        p.scale(1, 0.28);
        QRadialGradient g(QPointF(0, 0), rx);
        g.setColorAt(0, QColor(130, 8, 10, 240));
        g.setColorAt(0.85, QColor(85, 4, 7, 225));
        g.setColorAt(1, QColor(60, 0, 4, 0));
        p.setBrush(g);
        p.drawEllipse(QPointF(0, 0), rx, rx);
        p.restore();
    };
    pool(GX + 40, GY + 6, 130 * m_gobPool);
    pool(HX - 40, GY + 6, 80 * m_heroPool);
}

void ArenaWidget::drawGoblin(QPainter &p)
{
    double lunge = 0, arm, roar = 0;
    if (m_gobAtk >= 0) {
        lunge = -180 * qSin(PI * m_gobAtk);
        arm = swing(m_gobAtk, 10, -50, 85);
        roar = qSin(PI * qMin(1.0, m_gobAtk * 1.4));
    } else {
        arm = 10 + 4 * qSin(m_time * 2);
    }
    double shake = m_gobHit >= 0 ? qSin(m_gobHit * 50) * 12 * (1 - m_gobHit) : 0;
    double fl = m_gobHit >= 0 ? 0.6 * (1 - m_gobHit) : 0;
    auto C = [&](const QColor &c) { return mixColor(c, QColor(255, 40, 40), fl); };
    auto G = [&](double cx, double cy, double r, const QColor &a, const QColor &b) {
        QRadialGradient g(QPointF(cx, cy), r);
        g.setColorAt(0, C(a));
        g.setColorAt(1, C(b));
        return QBrush(g);
    };
    double dmgF = 1.0 - qBound(0.0, m_mShown / qMax(1, m_mMax), 1.0);

    p.save();
    p.translate(GX + lunge + shake, GY);
    if (m_gobDead) {
        p.rotate(m_gobDeadT * 85);
        p.setOpacity(1 - 0.35 * m_gobDeadT);
    }
    p.scale(0.8, 0.8 * (1 + 0.012 * qSin(m_time * 1.8)));
    p.setPen(Qt::NoPen);

    // shadow
    p.setBrush(QColor(0, 0, 0, 120));
    p.drawEllipse(QRectF(-120, -10, 240, 24));

    // back arm
    p.setBrush(C(QColor("#2f4a26")));
    p.drawRoundedRect(QRectF(70, -195, 28, 105), 13, 13);

    // legs and feet
    p.setBrush(G(0, -40, 60, QColor("#4d6c3b"), QColor("#22331c")));
    p.drawRoundedRect(QRectF(-55, -65, 42, 65), 10, 10);
    p.drawRoundedRect(QRectF(15, -65, 42, 65), 10, 10);
    p.setBrush(C(QColor("#1f2f19")));
    p.drawEllipse(QRectF(-74, -16, 64, 18));
    p.drawEllipse(QRectF(10, -16, 64, 18));

    // body
    p.setBrush(G(-25, -170, 130, QColor("#6d8f55"), QColor("#1f3019")));
    p.drawEllipse(QRectF(-90, -220, 180, 185));

    // muscle lines
    p.setPen(QPen(C(QColor("#17240f")), 3, Qt::SolidLine, Qt::RoundCap));
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(-62, -185, 62, 50), 200 * 16, 140 * 16);
    p.drawArc(QRectF(0, -185, 62, 50), 200 * 16, 140 * 16);
    p.drawLine(QPointF(0, -135), QPointF(0, -82));
    p.drawLine(QPointF(-25, -118), QPointF(25, -118));
    p.drawLine(QPointF(-22, -98), QPointF(22, -98));

    // old scars
    p.setPen(QPen(C(QColor("#b9c79a")), 2.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-55, -195), QPointF(5, -125));
    p.drawLine(QPointF(-45, -190), QPointF(-36, -180));
    p.drawLine(QPointF(-33, -176), QPointF(-24, -166));
    p.drawLine(QPointF(-21, -160), QPointF(-12, -150));

    // fresh wounds appear as the goblin loses HP
    static const double wd[6][4] = {{-58,-192,-12,-142},{22,-202,62,-152},{-32,-122,22,-102},
                                    {42,-132,72,-92},{-68,-152,-42,-102},{0,-212,32,-172}};
    int nw = int(dmgF * 6.5);
    for (int i = 0; i < nw && i < 6; ++i) {
        QPointF a(wd[i][0], wd[i][1]), b(wd[i][2], wd[i][3]);
        p.setPen(QPen(QColor(70, 4, 6), 8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        p.setPen(QPen(QColor(190, 14, 14), 3.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(a, b);
        p.setPen(QPen(QColor(150, 8, 10, 220), 4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(b, QPointF(b.x(), b.y() + 22 + i * 6 + 4 * qSin(m_time * 2 + i)));
    }
    p.setPen(Qt::NoPen);

    // bone necklace
    p.setBrush(QColor("#e8e1c8"));
    for (int i = 0; i < 7; ++i)
        p.drawEllipse(QPointF(-50 + i * 16.7, -207 + qSin(PI * i / 6) * 16), 6, 8);

    // loincloth and belt
    p.setBrush(C(QColor("#4d3820")));
    QPolygonF cloth;
    cloth << QPointF(-48, -70) << QPointF(48, -70) << QPointF(40, -30) << QPointF(28, -42)
          << QPointF(15, -18) << QPointF(0, -38) << QPointF(-14, -16) << QPointF(-28, -40)
          << QPointF(-40, -28);
    p.drawPolygon(cloth);
    p.setBrush(C(QColor("#251a0e")));
    p.drawRect(QRectF(-50, -80, 100, 12));
    p.setBrush(QColor("#e8e1c8"));
    p.drawEllipse(QPointF(0, -74), 10, 10);
    p.setBrush(QColor("#120a0a"));
    p.drawEllipse(QPointF(-4, -76), 2.5, 3);
    p.drawEllipse(QPointF(4, -76), 2.5, 3);

    // ears with gold rings
    p.setBrush(C(QColor("#3b5a2d")));
    QPolygonF earL, earR;
    earL << QPointF(-52, -275) << QPointF(-138, -300) << QPointF(-122, -285) << QPointF(-58, -238);
    earR << QPointF(52, -275) << QPointF(138, -300) << QPointF(122, -285) << QPointF(58, -238);
    p.drawPolygon(earL);
    p.drawPolygon(earR);
    p.setBrush(C(QColor("#8aa86f")));
    QPolygonF inL, inR;
    inL << QPointF(-58, -270) << QPointF(-112, -288) << QPointF(-62, -250);
    inR << QPointF(58, -270) << QPointF(112, -288) << QPointF(62, -250);
    p.drawPolygon(inL);
    p.drawPolygon(inR);
    p.setPen(QPen(QColor("#d8a920"), 3));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(-100, -272), 7, 7);
    p.drawEllipse(QPointF(100, -272), 7, 7);
    p.setPen(Qt::NoPen);

    // head
    p.setBrush(G(-10, -275, 85, QColor("#6d8f55"), QColor("#25381e")));
    p.drawEllipse(QRectF(-64, -318, 128, 114));

    // horns
    p.setBrush(C(QColor("#d9cfae")));
    QPainterPath hornL;
    hornL.moveTo(-46, -298);
    hornL.quadTo(-92, -312, -84, -354);
    hornL.lineTo(-64, -306);
    hornL.closeSubpath();
    p.drawPath(hornL);
    QPainterPath hornR;
    hornR.moveTo(46, -298);
    hornR.quadTo(92, -312, 84, -354);
    hornR.lineTo(64, -306);
    hornR.closeSubpath();
    p.drawPath(hornR);

    // rusty broken crown
    p.setBrush(QColor("#7a6530"));
    QPolygonF crown;
    crown << QPointF(-38, -300) << QPointF(-40, -340) << QPointF(-22, -318) << QPointF(-8, -350)
          << QPointF(6, -320) << QPointF(22, -344) << QPointF(40, -316) << QPointF(38, -300);
    p.drawPolygon(crown);
    p.setBrush(QColor(140, 10, 12));
    p.drawEllipse(QPointF(0, -310), 5, 5);

    // heavy brows
    p.setPen(QPen(C(QColor("#121c0b")), 9, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-60, -298), QPointF(-12, -274));
    p.drawLine(QPointF(60, -298), QPointF(12, -274));
    p.setPen(Qt::NoPen);

    // eyes
    if (m_gobDead) {
        p.setPen(QPen(QColor("#120808"), 4, Qt::SolidLine, Qt::RoundCap));
        for (int sx : {-1, 1}) {
            p.drawLine(QPointF(sx * 30 - 9, -280), QPointF(sx * 30 + 9, -262));
            p.drawLine(QPointF(sx * 30 + 9, -280), QPointF(sx * 30 - 9, -262));
        }
        p.setPen(Qt::NoPen);
    } else {
        double flick = 0.75 + 0.25 * qSin(m_time * 9);
        for (int sx : {-1, 1}) {
            double ex = sx * 30;
            QRadialGradient eg(QPointF(ex, -272), 34 * (1 + 0.5 * roar));
            eg.setColorAt(0, QColor(255, 190, 0, int(190 * flick)));
            eg.setColorAt(1, QColor(255, 60, 0, 0));
            p.setBrush(eg);
            p.drawEllipse(QPointF(ex, -272), 34 * (1 + 0.5 * roar), 34 * (1 + 0.5 * roar));
            p.setBrush(QColor(roar > 0.3 ? "#ff5a1a" : "#ffb300"));
            p.drawEllipse(QRectF(ex - 12, -280, 24, 17));
            p.setBrush(QColor("#0a0303"));
            p.drawEllipse(QRectF(ex - 2.5, -281, 5, 19));
        }
        // scar over left eye
        p.setPen(QPen(QColor("#c7d3a5"), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(-52, -302), QPointF(-20, -250));
        p.setPen(QPen(QColor("#8a1010"), 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(-52, -302), QPointF(-20, -250));
        p.setPen(Qt::NoPen);
    }

    // nose
    p.setBrush(C(QColor("#2a4020")));
    p.drawEllipse(QRectF(-11, -266, 22, 17));
    p.setBrush(QColor("#0a0303"));
    p.drawEllipse(QPointF(-4, -256), 2.3, 3.2);
    p.drawEllipse(QPointF(4, -256), 2.3, 3.2);

    // mouth: opens wide when roaring
    double mh = 16 + 36 * roar + (m_gobDead ? 22 : 0) + 2 * qSin(m_time * 1.8);
    double mTop = -246, mBot = mTop + 14 + mh;
    p.setBrush(QColor("#220707"));
    p.drawEllipse(QRectF(-37, mTop, 74, 14 + mh));
    if (mh > 20) {
        p.setBrush(QColor("#8a2a3a"));
        p.drawEllipse(QRectF(-18, mBot - 20, 36, 16));
    }
    p.setBrush(QColor("#efe7c8"));
    for (int i = 0; i < 7; ++i) {
        double x = -31 + i * 9.5;
        QPolygonF up;
        up << QPointF(x, mTop + 2) << QPointF(x + 8, mTop + 2) << QPointF(x + 4, mTop + 13);
        p.drawPolygon(up);
        QPolygonF lo;
        lo << QPointF(x, mBot - 2) << QPointF(x + 7, mBot - 2) << QPointF(x + 3.5, mBot - 12);
        p.drawPolygon(lo);
    }
    QPolygonF tuskL, tuskR;
    tuskL << QPointF(-36, mBot + 1) << QPointF(-25, mBot + 1) << QPointF(-31, mBot - 36);
    tuskR << QPointF(36, mBot + 1) << QPointF(25, mBot + 1) << QPointF(31, mBot - 36);
    p.drawPolygon(tuskL);
    p.drawPolygon(tuskR);
    // blood on the mouth and drool
    p.setPen(QPen(QColor(125, 8, 10), 4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-22, mBot + 2), QPointF(-22, mBot + 20 + 4 * qSin(m_time * 2)));
    p.setPen(QPen(QColor(215, 230, 200, 150), 2.2, Qt::SolidLine, Qt::RoundCap));
    double dl = 16 + 10 * qSin(m_time * 2.2);
    p.drawLine(QPointF(14, mBot + 1), QPointF(14, mBot + dl));
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(215, 230, 200, 150));
    p.drawEllipse(QPointF(14, mBot + dl + 2), 3, 4);

    // front arm with spiked, bloody club
    p.save();
    p.translate(-80, -180);
    p.rotate(arm);
    p.setBrush(G(0, 40, 60, QColor("#557a40"), QColor("#25381e")));
    p.drawRoundedRect(QRectF(-16, -8, 32, 98), 15, 15);
    p.setBrush(C(QColor("#3b2a18")));
    p.drawRoundedRect(QRectF(-9, 70, 18, 110), 8, 8);
    p.setBrush(C(QColor("#4a3520")));
    p.drawEllipse(QRectF(-26, 148, 52, 82));
    p.setBrush(QColor(150, 155, 165));
    for (int i = 0; i < 4; ++i) {
        double yy = 165 + i * 15;
        QPolygonF a1, a2;
        a1 << QPointF(-24, yy - 4) << QPointF(-37, yy) << QPointF(-24, yy + 4);
        a2 << QPointF(24, yy - 4) << QPointF(37, yy) << QPointF(24, yy + 4);
        p.drawPolygon(a1);
        p.drawPolygon(a2);
    }
    p.setBrush(QColor(115, 8, 10, 215));
    p.drawEllipse(QRectF(-18, 158, 22, 30));
    p.drawEllipse(QRectF(2, 188, 18, 24));
    p.setPen(QPen(QColor(125, 8, 10), 3, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-6, 186), QPointF(-6, 204 + 5 * qSin(m_time * 2)));
    p.setPen(Qt::NoPen);
    p.setBrush(C(QColor("#557a40")));
    p.drawEllipse(QRectF(-17, 62, 34, 34));
    p.setBrush(QColor("#e8e1c8"));
    for (int i = -1; i <= 1; ++i) {
        QPolygonF claw;
        claw << QPointF(i * 9 - 3, 90) << QPointF(i * 9 + 3, 90) << QPointF(i * 9, 104);
        p.drawPolygon(claw);
    }
    p.restore();

    p.restore();
}

void ArenaWidget::drawHero(QPainter &p)
{
    double lunge = 0, arm;
    if (m_heroAtk >= 0) {
        lunge = 230 * qSin(PI * m_heroAtk);
        arm = swing(m_heroAtk, -140, -175, -70);
    } else {
        arm = -140 + 3 * qSin(m_time * 2.5);
    }
    double shake = m_heroHit >= 0 ? qSin(m_heroHit * 50) * 9 * (1 - m_heroHit) : 0;
    double fl = m_heroHit >= 0 ? 0.6 * (1 - m_heroHit) : 0;
    double gl = 0;
    if (m_heal >= 0)
        gl = 0.4 * qMin(1.0, qMin(m_heal / 0.2, (1 - m_heal) / 0.2));
    auto C = [&](const QColor &c) {
        return mixColor(mixColor(c, QColor(255, 40, 40), fl), QColor(80, 255, 130), gl);
    };
    double dmgF = 1.0 - qBound(0.0, m_pShown / qMax(1, m_pMax), 1.0);

    p.save();
    p.translate(HX + lunge + shake, GY + qSin(m_time * 2.5));
    if (m_heroDead) {
        p.rotate(-m_heroDeadT * 85);
        p.setOpacity(1 - 0.35 * m_heroDeadT);
    }
    p.scale(1.3, 1.3);
    p.setPen(Qt::NoPen);

    p.setBrush(QColor(0, 0, 0, 110));
    p.drawEllipse(QRectF(-38, -6, 76, 12));

    // tattered cape
    double flutter = 6 * qSin(m_time * 5);
    QPainterPath cape;
    cape.moveTo(-12, -88);
    cape.lineTo(-44 - flutter, -22);
    cape.lineTo(-30 - flutter, -14);
    cape.lineTo(-20, -6);
    cape.lineTo(-10, -12);
    cape.lineTo(12, -80);
    p.setBrush(C(QColor("#8e1c14")));
    p.drawPath(cape);

    // legs
    p.setBrush(C(QColor("#4a5766")));
    p.drawRoundedRect(QRectF(-15, -38, 13, 38), 4, 4);
    p.drawRoundedRect(QRectF(2, -38, 13, 38), 4, 4);
    p.setBrush(C(QColor("#2c3540")));
    p.drawRoundedRect(QRectF(-17, -9, 18, 9), 3, 3);
    p.drawRoundedRect(QRectF(0, -9, 18, 9), 3, 3);

    // armor
    p.setBrush(C(QColor("#8da2b8")));
    p.drawRoundedRect(QRectF(-21, -90, 42, 56), 8, 8);
    p.setBrush(C(QColor("#5f7388")));
    p.drawRect(QRectF(-21, -44, 42, 7));
    p.setBrush(QColor("#d8a920"));
    p.drawEllipse(QPointF(0, -62), 6, 6);

    // blood on the armor as hero gets hurt
    static const double wd[6][4] = {{-14,-84,-4,-66},{4,-80,16,-60},{-16,-60,-6,-46},
                                    {6,-70,14,-52},{-10,-92,2,-78},{-18,-72,-12,-56}};
    int nw = int(dmgF * 6.5);
    p.setPen(QPen(QColor(125, 8, 10, 230), 3.2, Qt::SolidLine, Qt::RoundCap));
    for (int i = 0; i < nw && i < 6; ++i) {
        p.drawLine(QPointF(wd[i][0], wd[i][1]), QPointF(wd[i][2], wd[i][3]));
        p.drawLine(QPointF(wd[i][2], wd[i][3]), QPointF(wd[i][2], wd[i][3] + 8 + 2 * qSin(m_time * 2 + i)));
    }
    p.setPen(Qt::NoPen);

    // head and helmet
    p.setBrush(C(QColor("#e8bb8c")));
    p.drawEllipse(QPointF(0, -106), 17, 17);
    p.setBrush(C(QColor("#8da2b8")));
    p.drawPie(QRectF(-19, -126, 38, 38), 0, 180 * 16);
    p.setBrush(C(QColor("#5f7388")));
    p.drawRect(QRectF(-19, -108, 38, 5));
    p.setBrush(QColor("#c0261a"));
    QPolygonF plume;
    plume << QPointF(-3, -126) << QPointF(-18 - flutter * 0.5, -138) << QPointF(4, -132);
    p.drawPolygon(plume);
    p.setBrush(QColor("#1a1a2e"));
    p.drawEllipse(QPointF(8, -103), 2.2, 2.2);
    if (dmgF > 0.4) {
        p.setPen(QPen(QColor(125, 8, 10), 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(6, -112), QPointF(10, -96));
        p.setPen(Qt::NoPen);
    }

    // sword arm
    p.save();
    p.translate(16, -80);
    p.rotate(arm);
    p.setBrush(C(QColor("#8da2b8")));
    p.drawRoundedRect(QRectF(-6, -4, 12, 52), 5, 5);
    p.setBrush(C(QColor("#e8bb8c")));
    p.drawEllipse(QPointF(0, 50), 7, 7);
    p.setBrush(QColor("#d8a920"));
    p.drawRoundedRect(QRectF(-14, 52, 28, 7), 3, 3);
    p.setBrush(QColor("#5a3b1c"));
    p.drawRect(QRectF(-3, 44, 6, 10));
    QPolygonF blade;
    blade << QPointF(-5, 59) << QPointF(5, 59) << QPointF(4, 135) << QPointF(0, 148) << QPointF(-4, 135);
    p.setBrush(QColor("#e8f1fb"));
    p.drawPolygon(blade);
    p.setPen(QPen(QColor(255, 255, 255, 160), 1.5));
    p.drawLine(QPointF(0, 62), QPointF(0, 138));
    // goblin blood on the blade once the goblin is hurt
    if (m_mShown < m_mMax - 5) {
        p.setPen(QPen(QColor(130, 8, 10, 235), 3.5, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(0, 98), QPointF(0, 140));
        p.drawLine(QPointF(0, 140), QPointF(0, 146));
    }
    p.setPen(Qt::NoPen);
    p.restore();

    p.restore();
}

void ArenaWidget::drawPrince(QPainter &p)
{
    if (m_heal < 0)
        return;
    double a = qMin(1.0, qMin(m_heal / 0.12, (1 - m_heal) / 0.12));
    double px = 360 + (1 - a) * 40;
    double bob = qSin(m_time * 4) * 3;

    p.save();
    p.setOpacity(a);
    p.translate(px, GY + bob);
    p.scale(1.3, 1.3);
    p.setPen(Qt::NoPen);

    p.setBrush(QColor(0, 0, 0, 90));
    p.drawEllipse(QRectF(-34, -6, 68, 12));

    double flutter = 6 * qSin(m_time * 5);
    QPainterPath cape;
    cape.moveTo(12, -95);
    cape.lineTo(46 + flutter, -18);
    cape.lineTo(12, -6);
    cape.lineTo(-10, -90);
    p.setBrush(QColor("#7b1fa2"));
    p.drawPath(cape);
    p.setBrush(QColor("#f5f5f5"));
    p.drawRect(QRectF(8, -98, 8, 8));

    p.setBrush(QColor("#1a237e"));
    p.drawRoundedRect(QRectF(-13, -45, 11, 45), 4, 4);
    p.drawRoundedRect(QRectF(2, -45, 11, 45), 4, 4);
    p.setBrush(QColor("#ffd54f"));
    p.drawRoundedRect(QRectF(-15, -8, 15, 8), 3, 3);
    p.drawRoundedRect(QRectF(0, -8, 15, 8), 3, 3);

    p.setBrush(QColor("#2962ff"));
    p.drawRoundedRect(QRectF(-21, -102, 42, 62), 8, 8);
    p.setBrush(QColor("#ffd54f"));
    p.drawRect(QRectF(-21, -64, 42, 6));
    p.drawRect(QRectF(-3, -102, 6, 62));

    p.setBrush(QColor("#ffd54f"));
    p.drawEllipse(QPointF(2, -120), 19, 19);
    p.setBrush(QColor("#f5cba7"));
    p.drawEllipse(QPointF(-1, -117), 16, 16);
    p.setBrush(QColor("#ffcc00"));
    QPolygonF crown;
    crown << QPointF(-14, -130) << QPointF(-15, -148) << QPointF(-7, -139) << QPointF(0, -152)
          << QPointF(7, -139) << QPointF(15, -148) << QPointF(14, -130);
    p.drawPolygon(crown);
    p.setBrush(QColor("#e91e63"));
    p.drawEllipse(QPointF(0, -137), 3, 3);
    p.setBrush(QColor("#1a1a2e"));
    p.drawEllipse(QPointF(-8, -117), 2.2, 2.2);
    p.setPen(QPen(QColor("#a0522d"), 1.5));
    p.drawArc(QRectF(-10, -114, 10, 7), 200 * 16, 140 * 16);
    p.setPen(Qt::NoPen);

    // raised arm toward the hero
    p.setBrush(QColor("#2962ff"));
    p.drawRoundedRect(QRectF(-62, -99, 48, 12), 6, 6);
    p.setBrush(QColor("#f5cba7"));
    p.drawEllipse(QPointF(-64, -93), 7, 7);

    // glowing orb in his hand
    double pulse = 1 + 0.25 * qSin(m_time * 14);
    QRadialGradient og(QPointF(-72, -93), 30 * pulse);
    og.setColorAt(0, QColor(150, 255, 180, 230));
    og.setColorAt(1, QColor(60, 255, 120, 0));
    p.setBrush(og);
    p.drawEllipse(QPointF(-72, -93), 30 * pulse, 30 * pulse);
    p.setBrush(QColor(210, 255, 220));
    p.drawEllipse(QPointF(-72, -93), 7, 7);

    p.restore();
}

void ArenaWidget::drawEffects(QPainter &p)
{
    p.setPen(Qt::NoPen);

    // sword slash on the goblin
    if (m_slash >= 0) {
        double t = m_slash;
        int al = int(255 * (1 - t));
        double prog = qMin(1.0, t * 3);
        for (int k = 0; k < 3; ++k) {
            QPointF a(535 + k * 20, 165 + k * 8);
            QPointF b(a.x() + 62, a.y() + 135);
            QPointF e = a + (b - a) * prog;
            p.setPen(QPen(QColor(255, 255, 255, al), 8 * (1 - t) + 2, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a, e);
            p.setPen(QPen(QColor(200, 10, 10, al), 3, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(a + QPointF(3, 0), e + QPointF(3, 0));
        }
        QRadialGradient ig(QPointF(580, 235), 70);
        ig.setColorAt(0, QColor(255, 255, 255, int(190 * (1 - t))));
        ig.setColorAt(1, QColor(255, 120, 80, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(ig);
        p.drawEllipse(QPointF(580, 235), 70, 70);
    }

    // impact flash when the hero is hit
    if (m_heroHit >= 0) {
        QRadialGradient ig(QPointF(HX + 28, GY - 100), 60);
        ig.setColorAt(0, QColor(255, 120, 90, int(200 * (1 - m_heroHit))));
        ig.setColorAt(1, QColor(255, 60, 40, 0));
        p.setBrush(ig);
        p.drawEllipse(QPointF(HX + 28, GY - 100), 60, 60);
    }

    // healing effect
    if (m_heal >= 0) {
        double a = qMin(1.0, qMin(m_heal / 0.15, (1 - m_heal) / 0.2));

        QRadialGradient g(QPointF(HX, GY - 90), 150);
        g.setColorAt(0, QColor(80, 255, 130, int(160 * a)));
        g.setColorAt(1, QColor(80, 255, 130, 0));
        p.setBrush(g);
        p.drawEllipse(QPointF(HX, GY - 90), 150, 150);

        if (m_heal > 0.15 && m_heal < 0.85) {
            double pulse = 0.6 + 0.4 * qSin(m_time * 18);
            QPointF s0(266, GY - 121), s1(HX + 32, GY - 100);
            QPolygonF zig;
            for (int k = 0; k <= 8; ++k) {
                QPointF q = s0 + (s1 - s0) * (k / 8.0);
                if (k > 0 && k < 8) q.ry() += qSin(m_time * 40 + k * 1.7) * 7;
                zig << q;
            }
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(120, 255, 170, int(170 * a * pulse)), 12, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(zig);
            p.setPen(QPen(QColor(230, 255, 235, int(235 * a)), 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.drawPolyline(zig);
            p.setPen(Qt::NoPen);
        }

        double r = 25 + 105 * m_heal;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(100, 255, 150, int(220 * (1 - m_heal))), 4));
        p.drawEllipse(QPointF(HX, GY + 2), r, r * 0.28);
        p.setPen(Qt::NoPen);
    }

    // rising green plus signs
    for (const Particle &pt : m_particles) {
        int al = int(255 * qBound(0.0, pt.life, 1.0));
        p.setPen(QPen(QColor(110, 255, 160, al), 3, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(pt.x - 6, pt.y), QPointF(pt.x + 6, pt.y));
        p.drawLine(QPointF(pt.x, pt.y - 6), QPointF(pt.x, pt.y + 6));
    }
    p.setPen(Qt::NoPen);

    // torch embers
    for (const Particle &e : m_embers) {
        p.setBrush(QColor(255, 150, 40, int(230 * qBound(0.0, e.life, 1.0))));
        p.drawEllipse(QPointF(e.x, e.y), 2, 2);
    }
}

void ArenaWidget::drawBloodDrops(QPainter &p)
{
    for (const Blood &b : m_blood) {
        int al = int(255 * qBound(0.0, b.life * 2, 1.0));
        p.setPen(QPen(QColor(150, 8, 10, al), b.size, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(b.x - b.vx * 0.025, b.y - b.vy * 0.025), QPointF(b.x, b.y));
    }
    p.setPen(Qt::NoPen);
}

void ArenaWidget::drawFog(QPainter &p)
{
    p.setPen(Qt::NoPen);
    for (const Fog &f : m_fog) {
        QRadialGradient g(QPointF(f.x, f.y), f.r);
        g.setColorAt(0, QColor(190, 180, 210, 38));
        g.setColorAt(1, QColor(190, 180, 210, 0));
        p.setBrush(g);
        p.drawEllipse(QPointF(f.x, f.y), f.r, f.r * 0.3);
    }
}

void ArenaWidget::drawBar(QPainter &p, double x, double y, double w, const QString &name,
                          double shown, int real, int maxv,
                          const QColor &c1, const QColor &c2, bool right)
{
    QFont f("Georgia");
    f.setPixelSize(18);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0, 0, 0, 200));
    p.drawText(QRectF(x + 1, y - 27, w, 24),
               (right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, name);
    p.setPen(QColor(240, 230, 220));
    p.drawText(QRectF(x, y - 28, w, 24),
               (right ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, name);

    QRectF r(x, y, w, 20);
    p.setPen(QPen(QColor(200, 170, 120, 150), 2));
    p.setBrush(QColor(0, 0, 0, 190));
    p.drawRoundedRect(r, 6, 6);

    double frac = maxv > 0 ? qBound(0.0, shown / maxv, 1.0) : 0;
    if (frac > 0.001) {
        QRectF fr(x + 2, y + 2, (w - 4) * frac, 16);
        if (right)
            fr = QRectF(x + w - 2 - (w - 4) * frac, y + 2, (w - 4) * frac, 16);
        QLinearGradient g(fr.topLeft(), fr.bottomLeft());
        g.setColorAt(0, c1);
        g.setColorAt(1, c2);
        p.setPen(Qt::NoPen);
        p.setBrush(g);
        p.drawRoundedRect(fr, 5, 5);
    }

    QFont f2 = p.font();
    f2.setPixelSize(12);
    p.setFont(f2);
    p.setPen(Qt::white);
    p.drawText(r, Qt::AlignCenter, QString("%1 / %2").arg(real).arg(maxv));
}

// =====================================================================
//                            MainWindow
// =====================================================================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    setWindowTitle("RPG Combat Arena 3.0");
    resize(1000, 720);

    setStyleSheet(R"(
        QMainWindow, QWidget#root { background: #07070f; }
        QLabel { color: #f1f1f1; }
        QLabel#title { font-family: Georgia; font-size: 54px; font-weight: bold; color: #c0261a; letter-spacing: 6px; }
        QLabel#sub { font-family: Georgia; font-size: 18px; font-style: italic; color: #b9a99a; }
        QLabel#keys { font-size: 14px; color: #8a8a9a; }
        QLineEdit { background: #15121f; color: white; border: 2px solid #8e1c14;
                    border-radius: 10px; padding: 10px; font-size: 20px; }
        QPushButton { background: #2a2a45; color: white; border-radius: 10px;
                      padding: 14px; font-size: 17px; font-weight: bold; }
        QPushButton:hover { background: #3f3f6a; }
        QPushButton:disabled { background: #17172a; color: #5a5a70; }
        QPushButton#attack { background: #a52a1d; }
        QPushButton#attack:hover { background: #d13a2a; }
        QPushButton#heal { background: #1f8f50; }
        QPushButton#heal:hover { background: #2ecc71; }
        QPushButton#run { background: #4a5560; }
        QPushButton#attack:disabled, QPushButton#heal:disabled, QPushButton#run:disabled
            { background: #17172a; color: #5a5a70; }
        QPushButton#start, QPushButton#again { background: #b3261e; }
        QPushButton#start:hover, QPushButton#again:hover { background: #e03a2e; }
        QTextEdit { background: #08080f; color: #d8d0c8; border: 2px solid #2a2a45;
                    border-radius: 8px; font-size: 13px; }
    )");

    m_pages = new QStackedWidget;
    setCentralWidget(m_pages);

    // ---------- PAGE 0: start screen ----------
    QWidget *start = new QWidget;
    start->setObjectName("root");
    QVBoxLayout *sl = new QVBoxLayout(start);
    sl->setAlignment(Qt::AlignCenter);
    sl->setSpacing(18);

    QLabel *title = new QLabel("RPG COMBAT ARENA");
    title->setObjectName("title");
    title->setAlignment(Qt::AlignCenter);
    QLabel *sub = new QLabel("The Goblin King hungers for your blood...");
    sub->setObjectName("sub");
    sub->setAlignment(Qt::AlignCenter);

    m_nameEdit = new QLineEdit;
    m_nameEdit->setPlaceholderText("Enter your name, warrior");
    m_nameEdit->setMinimumWidth(360);
    m_nameEdit->setAlignment(Qt::AlignCenter);

    QPushButton *startBtn = new QPushButton("ENTER THE ARENA");
    startBtn->setObjectName("start");
    startBtn->setMinimumWidth(360);

    QLabel *keys = new QLabel("Controls:   A = Attack     H = Heal     Esc = Leave");
    keys->setObjectName("keys");
    keys->setAlignment(Qt::AlignCenter);

    sl->addWidget(title);
    sl->addWidget(sub);
    sl->addSpacing(20);
    sl->addWidget(m_nameEdit, 0, Qt::AlignCenter);
    sl->addWidget(startBtn, 0, Qt::AlignCenter);
    sl->addWidget(keys);
    m_pages->addWidget(start);

    // ---------- PAGE 1: battle screen ----------
    QWidget *battle = new QWidget;
    battle->setObjectName("root");
    QVBoxLayout *bl = new QVBoxLayout(battle);
    bl->setContentsMargins(10, 10, 10, 10);
    bl->setSpacing(8);

    m_arena = new ArenaWidget;
    bl->addWidget(m_arena, 1);

    m_log = new QTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumHeight(85);
    m_log->setFocusPolicy(Qt::NoFocus);
    bl->addWidget(m_log);

    QHBoxLayout *btns = new QHBoxLayout;
    m_attackBtn = new QPushButton("ATTACK  [A]");
    m_attackBtn->setObjectName("attack");
    m_healBtn = new QPushButton("HEAL  [H]");
    m_healBtn->setObjectName("heal");
    m_runBtn = new QPushButton("LEAVE  [Esc]");
    m_runBtn->setObjectName("run");
    for (QPushButton *b : {m_attackBtn, m_healBtn, m_runBtn}) {
        b->setFocusPolicy(Qt::NoFocus);
        btns->addWidget(b);
    }
    bl->addLayout(btns);

    m_againBtn = new QPushButton("PLAY AGAIN");
    m_againBtn->setObjectName("again");
    m_againBtn->setFocusPolicy(Qt::NoFocus);
    m_againBtn->hide();
    bl->addWidget(m_againBtn);

    m_pages->addWidget(battle);

    // ---------- connections ----------
    connect(startBtn, &QPushButton::clicked, this, &MainWindow::startGame);
    connect(m_nameEdit, &QLineEdit::returnPressed, this, &MainWindow::startGame);
    connect(m_attackBtn, &QPushButton::clicked, this, &MainWindow::onAttack);
    connect(m_healBtn, &QPushButton::clicked, this, &MainWindow::onHeal);
    connect(m_runBtn, &QPushButton::clicked, this, &MainWindow::onRun);
    connect(m_againBtn, &QPushButton::clicked, this, &MainWindow::backToStart);

    // ---------- keyboard: A = attack, H = heal, Esc = leave ----------
    auto *kA = new QShortcut(QKeySequence(Qt::Key_A), this);
    connect(kA, &QShortcut::activated, this, &MainWindow::tryAttack);
    auto *kH = new QShortcut(QKeySequence(Qt::Key_H), this);
    connect(kH, &QShortcut::activated, this, &MainWindow::tryHeal);
    auto *kE = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(kE, &QShortcut::activated, this, &MainWindow::tryLeave);
}

void MainWindow::tryAttack()
{
    if (m_pages->currentIndex() == 1 && m_attackBtn->isEnabled())
        onAttack();
}

void MainWindow::tryHeal()
{
    if (m_pages->currentIndex() == 1 && m_healBtn->isEnabled())
        onHeal();
}

void MainWindow::tryLeave()
{
    if (m_pages->currentIndex() == 0)
        close();
    else if (m_over)
        backToStart();
    else
        onRun();
}

void MainWindow::startGame()
{
    QString name = m_nameEdit->text().trimmed();
    if (name.isEmpty())
        name = "Hero";

    m_session++;
    m_over = false;
    m_player  = {name, 100, 100, 10, 20};
    m_monster = {"Goblin King", 100, 100, 8, 15};
    m_potions = 3;

    m_arena->reset();
    m_arena->setNames(m_player.name, m_monster.name);
    m_log->clear();
    m_againBtn->hide();
    setActionsEnabled(true);
    log("<b style='color:#c0261a'>The gates slam shut behind you...</b>");
    log(QString("<b>%1</b> vs <b>%2</b> - FIGHT!").arg(m_player.name, m_monster.name));

    updateUi(true);
    m_pages->setCurrentIndex(1);
    m_arena->showTitle("THE GOBLIN KING AWAITS...");
}

void MainWindow::onAttack()
{
    if (m_over) return;
    setActionsEnabled(false);
    m_arena->heroAttack();
    const int id = m_session;

    // damage lands at the moment the sword strikes
    QTimer::singleShot(330, this, [this, id]() {
        if (!alive(id)) return;
        int damage = QRandomGenerator::global()->bounded(m_player.minDamage, m_player.maxDamage + 1);
        bool crit = damage >= 18;
        m_monster.health = qMax(0, m_monster.health - damage);
        m_arena->showDamage(true, damage, crit);
        log(QString("%1You slash the %2 for <b style='color:#e74c3c'>%3</b> damage!")
                .arg(crit ? "<b style='color:#ffcc00'>CRITICAL HIT! </b>" : "")
                .arg(m_monster.name).arg(damage));
        updateUi();

        if (m_monster.health <= 0) {
            m_arena->setDefeated(false, true);
            QTimer::singleShot(1800, this, [this, id]() {
                if (!alive(id)) return;
                endBattle(QString("The %1 is dead. The arena is yours.").arg(m_monster.name),
                          "#d8a920", "VICTORY");
            });
        } else {
            QTimer::singleShot(900, this, [this, id]() {
                if (alive(id)) monsterTurn();
            });
        }
    });
}

void MainWindow::onHeal()
{
    if (m_over || m_potions <= 0)
        return;
    setActionsEnabled(false);
    m_potions--;
    m_arena->heal();
    const int id = m_session;

    QTimer::singleShot(700, this, [this, id]() {
        if (!alive(id)) return;
        int amount = QRandomGenerator::global()->bounded(20, 31);
        int before = m_player.health;
        m_player.health = qMin(m_player.maxHealth, m_player.health + amount);
        int gained = m_player.health - before;
        m_arena->showHeal(gained);
        log(QString("The Prince heals you for <b style='color:#2ecc71'>%1</b> HP!").arg(gained));
        updateUi();
    });
    QTimer::singleShot(1900, this, [this, id]() {
        if (alive(id)) monsterTurn();
    });
}

void MainWindow::onRun()
{
    if (m_over) return;
    log("<span style='color:#e74c3c'>You fled from battle like a coward!</span>");
    endBattle("You fled from the arena like a coward.", "#9aa5b1", "YOU FLED");
}

void MainWindow::monsterTurn()
{
    m_arena->goblinAttack();
    const int id = m_session;

    QTimer::singleShot(450, this, [this, id]() {
        if (!alive(id)) return;
        int damage = QRandomGenerator::global()->bounded(m_monster.minDamage, m_monster.maxDamage + 1);
        bool crit = damage >= 14;
        m_player.health = qMax(0, m_player.health - damage);
        m_arena->showDamage(false, damage, crit);
        log(QString("%1The %2 smashes you for <b style='color:#e74c3c'>%3</b> damage!")
                .arg(crit ? "<b style='color:#ffcc00'>BRUTAL BLOW! </b>" : "")
                .arg(m_monster.name).arg(damage));
        updateUi();

        if (m_player.health <= 0) {
            m_arena->setDefeated(true, false);
            QTimer::singleShot(1800, this, [this, id]() {
                if (!alive(id)) return;
                endBattle(QString("You were crushed by the %1.").arg(m_monster.name),
                          "#c0261a", "YOU DIED");
            });
        } else {
            QTimer::singleShot(700, this, [this, id]() {
                if (alive(id)) setActionsEnabled(true);
            });
        }
    });
}

void MainWindow::backToStart()
{
    m_pages->setCurrentIndex(0);
    m_nameEdit->setFocus();
}

void MainWindow::updateUi(bool snap)
{
    m_arena->setHealth(m_player.health, m_player.maxHealth,
                       m_monster.health, m_monster.maxHealth, snap);
    m_healBtn->setText(QString("HEAL  [H]  x%1").arg(m_potions));
}

void MainWindow::log(const QString &html)
{
    m_log->append(html);
}

void MainWindow::setActionsEnabled(bool on)
{
    m_attackBtn->setEnabled(on);
    m_healBtn->setEnabled(on && m_potions > 0);
    m_runBtn->setEnabled(on);
}

void MainWindow::endBattle(const QString &message, const QString &color, const QString &banner)
{
    m_over = true;
    setActionsEnabled(false);
    log(QString("<b style='font-size:15px; color:%1'>%2</b>").arg(color, banner));
    m_arena->showBanner(banner, message, QColor(color));
    m_againBtn->show();
}