#include "dfinstancenix.h"
#include "truncatingfilelogger.h"
#include <QCryptographicHash>
#include <QFile>
#include <QTextCodec>
#include <string>

struct STLStringHeader {
    quint32 length;
    quint32 capacity;
    qint32 refcnt;
};

DFInstanceNix::DFInstanceNix(QObject *parent)
    : DFInstance(parent)
    , m_pid(0)
{}

QString DFInstanceNix::calculate_checksum() {
    // ELF binaries don't seem to store a linker timestamp, so just MD5 the file.
    QFile proc(m_loc_of_dfexe);
    QCryptographicHash hash(QCryptographicHash::Md5);
    if (!proc.open(QIODevice::ReadOnly)) {
        LOGE << "FAILED TO READ DF EXECUTABLE:" << m_loc_of_dfexe;
        return QString("UNKNOWN");
    }
    // Qt 4 doesn't support QCryptographicHash::addData(QIODevice*)
    m_buffer.resize(4096);
    qint64 len;
    while ((len = proc.read(m_buffer.data(), m_buffer.length())) > 0) {
        hash.addData(m_buffer.data(), len);
    }
    QString md5 = hexify(hash.result().mid(0, 4)).toLower();
    TRACE << "GOT MD5:" << md5;
    return md5;
}

QString DFInstanceNix::read_string(const VIRTADDR &addr) {
    m_buffer.resize(1024);
    read_raw(read_addr(addr), m_buffer.length(), m_buffer.data());
    return QTextCodec::codecForName("IBM437")->toUnicode(m_buffer.data());
}

bool DFInstanceNix::df_running(){
    pid_t cur_pid = m_pid;
    return (set_pid() && cur_pid == m_pid);
}

USIZE DFInstanceNix::write_string(const VIRTADDR &addr, const QString &str) {
    // Ensure this operation is done as one transaction
    attach();
    VIRTADDR buffer_addr = get_string(str);
    if (buffer_addr)
        // This unavoidably leaks the old buffer; our own
        // cannot be deallocated anyway.
        write_raw(addr, sizeof(VIRTADDR), &buffer_addr);
    detach();
    return buffer_addr ? str.length() : 0;
}

VIRTADDR DFInstanceNix::get_string(const QString &str) {
    if (m_string_cache.contains(str))
        return m_string_cache[str];

    QByteArray data = QTextCodec::codecForName("IBM437")->fromUnicode(str);

    STLStringHeader header;
    header.capacity = header.length = data.length();
    header.refcnt = -1; // huge refcnt to avoid dealloc

    QByteArray buf((char*)&header, sizeof(header));
    buf.append(data);
    buf.append(char(0));

    VIRTADDR addr = alloc_chunk(buf.length());

    if (addr) {
        write_raw(addr, buf.length(), buf.data());
        addr += sizeof(header);
    }

    return m_string_cache[str] = addr;
}
