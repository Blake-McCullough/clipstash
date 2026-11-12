#include "hotkeys/EvdevHotkeys.h"

#include <QDir>
#include <QSocketNotifier>
#include <QTimer>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

static bool testBit(const unsigned long *bits, int bit)
{
    return bits[bit / (8 * sizeof(unsigned long))] & (1UL << (bit % (8 * sizeof(unsigned long))));
}

EvdevHotkeys::EvdevHotkeys(QObject *parent) : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // udev needs a moment to apply permissions to a freshly plugged device.
        QTimer::singleShot(700, this, [this] { if (m_started) scan(); });
    });
}

EvdevHotkeys::~EvdevHotkeys() { stop(); }

bool EvdevHotkeys::start()
{
    m_started = true;
    if (m_watcher.directories().isEmpty()) m_watcher.addPath("/dev/input");
    scan();
    return isActive();
}

void EvdevHotkeys::stop()
{
    m_started = false;
    const QList<int> fds = m_devices.keys();
    for (int fd : fds) closeDevice(fd);
    m_pressed.clear();
}

void EvdevHotkeys::scan()
{
    QSet<QString> open;
    for (const Device &d : std::as_const(m_devices)) open.insert(d.path);
    const QStringList nodes = QDir("/dev/input").entryList({"event*"}, QDir::System);
    for (const QString &n : nodes) {
        const QString path = "/dev/input/" + n;
        if (!open.contains(path)) openDevice(path);
    }
}

void EvdevHotkeys::openDevice(const QString &path)
{
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return;

    unsigned long evBits[EV_MAX / (8 * sizeof(unsigned long)) + 1] = {};
    unsigned long keyBits[KEY_MAX / (8 * sizeof(unsigned long)) + 1] = {};
    const bool isKeyboard = ioctl(fd, EVIOCGBIT(0, sizeof(evBits)), evBits) >= 0
        && testBit(evBits, EV_KEY)
        && ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keyBits)), keyBits) >= 0
        && testBit(keyBits, KEY_A) && testBit(keyBits, KEY_ENTER);
    if (!isKeyboard) {
        ::close(fd);
        return;
    }

    auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this, fd] { readDevice(fd); });
    m_devices.insert(fd, {path, notifier});
}

void EvdevHotkeys::closeDevice(int fd)
{
    auto it = m_devices.find(fd);
    if (it == m_devices.end()) return;
    it->notifier->setEnabled(false);
    it->notifier->deleteLater();
    m_devices.erase(it);
    ::close(fd);
}

quint32 EvdevHotkeys::currentMods() const
{
    quint32 m = 0;
    if (m_pressed.contains(KEY_LEFTCTRL) || m_pressed.contains(KEY_RIGHTCTRL)) m |= ModCtrl;
    if (m_pressed.contains(KEY_LEFTSHIFT) || m_pressed.contains(KEY_RIGHTSHIFT)) m |= ModShift;
    if (m_pressed.contains(KEY_LEFTALT) || m_pressed.contains(KEY_RIGHTALT)) m |= ModAlt;
    if (m_pressed.contains(KEY_LEFTMETA) || m_pressed.contains(KEY_RIGHTMETA)) m |= ModMeta;
    return m;
}

void EvdevHotkeys::readDevice(int fd)
{
    input_event ev[32];
    for (;;) {
        const ssize_t n = ::read(fd, ev, sizeof(ev));
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) return;
            closeDevice(fd);  // unplugged
            return;
        }
        if (n == 0) return;
        const int count = int(n / sizeof(input_event));
        for (int i = 0; i < count; ++i) {
            if (ev[i].type != EV_KEY) continue;
            const int code = ev[i].code;
            if (ev[i].value == 0) {
                m_pressed.remove(code);
            } else if (ev[i].value == 1) {
                m_pressed.insert(code);
                const quint32 mods = currentMods();
                for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it)
                    if (it->code == code && it->code != 0 && it->mods == mods)
                        emit activated(it.key());
            }
        }
    }
}
