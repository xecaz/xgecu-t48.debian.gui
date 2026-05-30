// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProgrammerWorker.h"

#include <QByteArray>
#include <QString>
#include <cstring>
#include <vector>

extern "C" {
#include "database.h"
#include "minipro.h"
}

namespace {
QString modelName(int version)
{
    switch (version) {
    case MP_TL866A:      return "TL866A";
    case MP_TL866CS:     return "TL866CS";
    case MP_TL866IIPLUS: return "TL866II+";
    case MP_T56:         return "T56";
    case MP_T48:         return "T48";
    case MP_T76:         return "T76";
    default:             return QString("unknown(%1)").arg(version);
    }
}
// minipro masks unused bits high (value |= ~mask) for fuses AND locks alike.
// We keep the significant value for display: a byte-wide fuse (mask <= 0xFF)
// is presented as 8 bits (e.g. lfuse 0x62), wider fuses as their full word.
quint16 fuseSig(quint16 raw, quint16 mask)
{
    quint16 v = raw | static_cast<quint16>(~mask);
    if (mask <= 0xFF)
        v &= 0xFF;
    return v;
}

// The exact bytes minipro writes: significant value with unused bits forced
// high. format_int then lays this down little-endian across `word_size` bytes,
// so a byte-wide fuse on a word_size==2 part writes e.g. [0x62, 0xFF].
quint16 fuseWire(quint16 sig, quint16 mask)
{
    return sig | static_cast<quint16>(~mask);
}

// Build the declared fuse/lock layout from device->config (a fuse_decl_t).
// Returns an invalid FuseSet for chips with no config section or PLDs.
// Values are seeded to the manufacturer default until an actual read fills them.
FuseSet fuseLayout(const device_t *dev)
{
    FuseSet set;
    if (!dev || !dev->config || dev->chip_type == MP_PLD)
        return set;
    const auto *decl = reinterpret_cast<const fuse_decl_t *>(dev->config);
    if (decl->num_fuses == 0 && decl->num_locks == 0)
        return set;

    set.wordSize = dev->flags.word_size ? dev->flags.word_size : 1;
    set.lockReadable = !dev->flags.lock_bit_write_only;
    for (uint8_t i = 0; i < decl->num_fuses && i < 16; ++i) {
        FuseItem it;
        it.name = QString::fromUtf8(decl->fuse[i].name);
        it.mask = decl->fuse[i].mask;
        it.def = fuseSig(decl->fuse[i].def, decl->fuse[i].mask);
        it.value = it.def;
        it.isLock = false;
        set.items.append(it);
    }
    for (uint8_t i = 0; i < decl->num_locks && i < 4; ++i) {
        FuseItem it;
        it.name = QString::fromUtf8(decl->lock[i].name);
        it.mask = decl->lock[i].mask;
        it.def = fuseSig(decl->lock[i].def, decl->lock[i].mask);
        it.value = it.def;
        it.isLock = true;
        set.items.append(it);
    }
    set.valid = !set.items.isEmpty();
    return set;
}
}  // namespace

ProgrammerWorker::ProgrammerWorker(QObject *parent) : QObject(parent)
{
    // minipro's per-action C functions read settings off handle->cmdopts.
    // We allocate one zero-initialized struct (safe defaults for every flag)
    // and bind it to the handle once we open it.
    m_cmdopts = static_cast<cmdopts_t *>(calloc(1, sizeof(cmdopts_t)));
}

ProgrammerWorker::~ProgrammerWorker()
{
    closeHandle();
    free(m_cmdopts);
    m_cmdopts = nullptr;
}

void ProgrammerWorker::closeHandle()
{
    if (!m_handle)
        return;
    if (m_handle->device) {
        free(m_handle->device);  // get_device_by_name allocates with malloc
        m_handle->device = nullptr;
    }
    m_handle->cmdopts = nullptr;  // don't let minipro_close free our struct
    minipro_close(m_handle);
    m_handle = nullptr;
    m_currentChip.clear();
}

void ProgrammerWorker::detect()
{
    closeHandle();
    m_handle = minipro_open(NO_VERBOSE);
    if (!m_handle) {
        emit detectionFailed(
            "No programmer detected. Plug in the device and ensure udev rules "
            "are installed (third_party/minipro/udev/).");
        return;
    }
    m_handle->cmdopts = m_cmdopts;  // safe-default cmdopts for tx code paths

    DeviceInfo info;
    info.connected = true;
    info.versionId = m_handle->version;
    info.model = modelName(m_handle->version);
    info.firmwareStr = QString::fromUtf8(m_handle->firmware_str);
    info.deviceCode = QString::fromUtf8(m_handle->device_code);
    emit detected(info);
}

void ProgrammerWorker::openChip(const QString &miniproName)
{
    if (!m_handle) {
        emit chipOpenFailed("Programmer not connected");
        return;
    }
    if (m_handle->device) {
        free(m_handle->device);
        m_handle->device = nullptr;
    }

    db_data_t db{};
    const QByteArray nameUtf8 = miniproName.toUtf8();
    const QByteArray infoicUtf8 = m_infoicPath.toUtf8();
    const QByteArray logicicUtf8 = m_logicicPath.toUtf8();
    db.device_name = nameUtf8.constData();
    db.infoic_path = m_infoicPath.isEmpty() ? nullptr : infoicUtf8.constData();
    db.logicic_path = m_logicicPath.isEmpty() ? nullptr : logicicUtf8.constData();
    db.prog_version = m_handle->version;

    device_t *dev = get_device_by_name(&db);
    if (!dev) {
        emit chipOpenFailed(QString("Chip '%1' not found in minipro database for %2")
                                .arg(miniproName, modelName(m_handle->version)));
        return;
    }
    m_handle->device = dev;
    m_currentChip = miniproName;
    emit chipOpened(miniproName, dev->code_memory_size, dev->data_memory_size,
                    static_cast<bool>(dev->flags.can_erase));
    emit fusesAvailable(fuseLayout(dev));
}

namespace {
quint8 areaToMp(MemArea a) { return (a == MemArea::Data) ? MP_DATA : MP_CODE; }
quint32 areaTotalSize(const device_t *dev, MemArea a)
{
    return (a == MemArea::Data) ? dev->data_memory_size : dev->code_memory_size;
}
const char *areaName(MemArea a) { return (a == MemArea::Data) ? "data" : "code"; }
}  // namespace

void ProgrammerWorker::readMemory(MemArea area)
{
    m_cancel.store(false);

    ReadResult r;
    r.area = area;
    if (!m_handle || !m_handle->device) {
        r.errorMessage = "No chip selected. Pick a chip first.";
        emit readFinished(r);
        return;
    }
    device_t *dev = m_handle->device;
    const quint32 total = areaTotalSize(dev, area);
    if (total == 0) {
        r.errorMessage = QString("Chip has no %1 memory.").arg(areaName(area));
        emit readFinished(r);
        return;
    }
    const quint32 blockSize = dev->read_buffer_size
                                  ? dev->read_buffer_size
                                  : 64;
    const quint8 mpType = areaToMp(area);

    std::vector<uint8_t> buf(total, 0xFF);

    if (m_handle->minipro_begin_transaction
        && m_handle->minipro_begin_transaction(m_handle)) {
        r.errorMessage = "begin_transaction failed";
        emit readFinished(r);
        return;
    }

    data_set_t ds{};
    ds.data = buf.data();
    ds.type = mpType;
    ds.size = blockSize;
    ds.init = 1;
    ds.block_count = (total + blockSize - 1) / blockSize;

    const quint32 offset = dev->flags.has_data_offset ? dev->page_size : 0;
    // The word-organised address shift only applies to code memory.
    const bool wordOrg = (area == MemArea::Code)
        && (dev->flags.data_org == MP_ORG_WORDS);

    bool ok = true;
    qint64 done = 0;
    emit progress(0, total);

    for (uint32_t i = 0; i < ds.block_count; ++i) {
        if (m_cancel.load()) {
            r.errorMessage = "Read cancelled";
            ok = false;
            break;
        }
        ds.address = i * blockSize + offset;
        if (wordOrg)
            ds.address >>= 1;
        const int rc = m_handle->minipro_read_block
                           ? m_handle->minipro_read_block(m_handle, &ds)
                           : -1;
        if (rc) {
            ok = false;
            r.errorMessage = QString("read_block failed at offset 0x%1")
                                 .arg(i * blockSize, 0, 16);
            break;
        }
        ds.data += ds.size;
        ds.init = 0;
        done += ds.size;
        emit progress(qMin<qint64>(done, total), total);
    }

    if (m_handle->minipro_end_transaction)
        m_handle->minipro_end_transaction(m_handle);

    r.ok = ok;
    if (ok)
        r.data = QByteArray(reinterpret_cast<const char *>(buf.data()),
                            static_cast<qsizetype>(total));
    emit readFinished(r);
}

void ProgrammerWorker::verifyMemory(MemArea area, const QByteArray &expected)
{
    m_cancel.store(false);

    VerifyResult v;
    v.area = area;

    if (!m_handle || !m_handle->device) {
        v.errorMessage = "No chip selected. Pick a chip first.";
        emit verifyFinished(v);
        return;
    }
    device_t *dev = m_handle->device;
    const quint32 total = areaTotalSize(dev, area);
    if (total == 0) {
        v.errorMessage = QString("Chip has no %1 memory.").arg(areaName(area));
        emit verifyFinished(v);
        return;
    }
    if (static_cast<quint32>(expected.size()) != total) {
        v.errorMessage = QString(
            "Buffer size %1 bytes does not match chip %2 memory %3 bytes. "
            "Resize the buffer or reload a matching file before verifying.")
                             .arg(expected.size()).arg(areaName(area)).arg(total);
        emit verifyFinished(v);
        return;
    }
    const quint32 blockSize = dev->read_buffer_size ? dev->read_buffer_size : 64;
    const quint8 mpType = areaToMp(area);
    std::vector<uint8_t> buf(blockSize, 0xFF);

    if (m_handle->minipro_begin_transaction
        && m_handle->minipro_begin_transaction(m_handle)) {
        v.errorMessage = "begin_transaction failed";
        emit verifyFinished(v);
        return;
    }

    data_set_t ds{};
    ds.data = buf.data();
    ds.type = mpType;
    ds.size = blockSize;
    ds.init = 1;
    ds.block_count = (total + blockSize - 1) / blockSize;

    const quint32 offset = dev->flags.has_data_offset ? dev->page_size : 0;
    const bool wordOrg = (area == MemArea::Code)
        && (dev->flags.data_org == MP_ORG_WORDS);

    bool ioOk = true;
    qint64 done = 0;
    emit progress(0, total);

    const auto *expectedPtr = reinterpret_cast<const uint8_t *>(expected.constData());

    for (uint32_t i = 0; i < ds.block_count; ++i) {
        if (m_cancel.load()) {
            v.errorMessage = "Verify cancelled";
            ioOk = false;
            break;
        }
        const quint32 blockByteOffset = i * blockSize;
        ds.address = blockByteOffset + offset;
        if (wordOrg)
            ds.address >>= 1;
        const int rc = m_handle->minipro_read_block
                           ? m_handle->minipro_read_block(m_handle, &ds)
                           : -1;
        if (rc) {
            v.errorMessage = QString("read_block failed at offset 0x%1")
                                 .arg(blockByteOffset, 0, 16);
            ioOk = false;
            break;
        }

        const quint32 thisLen = qMin<quint32>(blockSize, total - blockByteOffset);
        for (quint32 j = 0; j < thisLen; ++j) {
            const quint32 off = blockByteOffset + j;
            if (buf[j] != expectedPtr[off]) {
                if (v.firstMismatchOffset < 0) {
                    v.firstMismatchOffset = off;
                    v.firstChipByte = buf[j];
                    v.firstExpectedByte = expectedPtr[off];
                }
                ++v.mismatches;
            }
            ++v.bytesChecked;
        }

        ds.init = 0;
        done += thisLen;
        emit progress(qMin<qint64>(done, total), total);
    }

    if (m_handle->minipro_end_transaction)
        m_handle->minipro_end_transaction(m_handle);

    if (!ioOk) {
        emit verifyFinished(v);
        return;
    }
    v.ok = (v.mismatches == 0);
    emit verifyFinished(v);
}

namespace {
// Read the first ≤64 bytes of code memory in an already-open transaction.
// Returns true if the read succeeded; sets *allFF accordingly.
bool probeFirstBlockAllFF(minipro_handle_t *h, bool *allFF)
{
    *allFF = false;
    device_t *dev = h->device;
    const quint32 probeSize = qMin<quint32>(
        dev->read_buffer_size ? dev->read_buffer_size : 64, 64);
    std::vector<uint8_t> probe(probeSize, 0);
    data_set_t ds{};
    ds.data = probe.data();
    ds.type = MP_CODE;  // presence probe always reads code memory
    ds.size = probeSize;
    ds.init = 1;
    ds.block_count = 1;
    ds.address = dev->flags.has_data_offset ? dev->page_size : 0;
    if (dev->flags.data_org == MP_ORG_WORDS)
        ds.address >>= 1;
    if (!h->minipro_read_block || h->minipro_read_block(h, &ds) != 0)
        return false;
    bool ff = true;
    for (quint32 i = 0; i < probeSize; ++i)
        if (probe[i] != 0xFF) { ff = false; break; }
    *allFF = ff;
    return true;
}
}  // namespace

void ProgrammerWorker::eraseChip(bool force)
{
    if (!m_handle || !m_handle->device) {
        emit eraseFinished(false, "No chip selected.");
        return;
    }
    device_t *dev = m_handle->device;
    if (!dev->flags.can_erase) {
        emit eraseFinished(false,
            "This chip is not electrically erasable (UV EPROM, OTP, or "
            "similar). Nothing was attempted.");
        return;
    }
    if (!m_handle->minipro_begin_transaction
        || !m_handle->minipro_end_transaction
        || !m_handle->minipro_read_block) {
        emit eraseFinished(false, "Programmer does not expose erase protocol.");
        return;
    }

    // Mirror minipro CLI's erase_device(): different num_fuses / pld arg
    // depending on chip family.
    uint8_t num_fuses = 0;
    uint8_t pld = 0;
    if (dev->config) {
        if (dev->chip_type == MP_PLD) {
            const uint8_t v = m_handle->version;
            if (v == MP_TL866IIPLUS || v == MP_T48 || v == MP_T56
                || v == MP_T76) {
                pld = (dev->protocol_id == IC2_ALG_GAL22) ? ERASE_PLD1
                                                          : ERASE_PLD2;
            }
        } else {
            auto *fuses = reinterpret_cast<fuse_decl_t *>(dev->config);
            num_fuses = (fuses->num_fuses > 4) ? 1 : fuses->num_fuses;
        }
    }

    if (m_handle->minipro_begin_transaction(m_handle)) {
        emit eraseFinished(false, "begin_transaction failed");
        return;
    }

    // Presence check: read the first block and refuse if it's all 0xFF.
    // The T48 has no hardware "is chip seated" signal, so an empty socket
    // floats high on most parts and reads as 0xFF — indistinguishable from
    // a genuinely blank chip. Either way, erasing without warning is wrong.
    if (!force && dev->code_memory_size > 0) {
        bool allFF = false;
        if (probeFirstBlockAllFF(m_handle, &allFF) && allFF) {
            m_handle->minipro_end_transaction(m_handle);
            emit eraseFinished(false,
                "Pre-erase check: first block reads as all 0xFF — "
                "either the chip is already blank, or no chip is "
                "in the socket. Refusing to erase.\n\n"
                "If you're sure the chip is seated and intentionally "
                "want to issue the erase anyway, enable the Force "
                "checkbox in the Erase dialog and try again.");
            return;
        }
    }

    const int rc = minipro_erase(m_handle, num_fuses, pld);
    m_handle->minipro_end_transaction(m_handle);
    if (rc) {
        emit eraseFinished(false, "minipro_erase failed");
        return;
    }
    emit eraseFinished(true, QString());
}

void ProgrammerWorker::readFuses()
{
    FuseSet set;
    if (!m_handle || !m_handle->device) {
        set.errorMessage = "No chip selected.";
        emit fusesRead(set);
        return;
    }
    set = fuseLayout(m_handle->device);
    if (!set.valid) {
        set.errorMessage = "This chip has no configuration fuses.";
        emit fusesRead(set);
        return;
    }
    if (!m_handle->minipro_begin_transaction || !m_handle->minipro_end_transaction) {
        set.errorMessage = "Programmer does not expose the fuse protocol.";
        emit fusesRead(set);
        return;
    }

    const size_t ws = set.wordSize;
    int numFuses = 0, numLocks = 0;
    for (const auto &it : set.items) (it.isLock ? numLocks : numFuses)++;

    if (m_handle->minipro_begin_transaction(m_handle)) {
        set.errorMessage = "begin_transaction failed";
        emit fusesRead(set);
        return;
    }

    uint8_t buffer[64] = {0};
    bool ioError = false;

    // Config-fuse section: mask unused bits high, mirroring the minipro CLI.
    if (numFuses > 0) {
        if (minipro_read_fuses(m_handle, MP_FUSE_CFG, numFuses * ws,
                               static_cast<uint8_t>(numFuses), buffer)) {
            ioError = true;
        } else {
            int idx = 0;
            for (auto &it : set.items) {
                if (it.isLock) continue;
                const quint16 raw = static_cast<quint16>(
                    load_int(&buffer[idx * ws], ws, MP_LITTLE_ENDIAN));
                it.value = fuseSig(raw, it.mask);
                ++idx;
            }
        }
    }

    // Lock section: items_count is word_size here (per the CLI). Skipped when
    // the part's lock bits are write-only.
    if (!ioError && numLocks > 0 && set.lockReadable) {
        std::memset(buffer, 0, sizeof(buffer));
        if (minipro_read_fuses(m_handle, MP_FUSE_LOCK, numLocks * ws,
                               static_cast<uint8_t>(ws), buffer)) {
            ioError = true;
        } else {
            int idx = 0;
            for (auto &it : set.items) {
                if (!it.isLock) continue;
                const quint16 raw = static_cast<quint16>(
                    load_int(&buffer[idx * ws], ws, MP_LITTLE_ENDIAN));
                it.value = fuseSig(raw, it.mask);
                ++idx;
            }
        }
    }

    m_handle->minipro_end_transaction(m_handle);
    if (ioError)
        set.errorMessage = "Reading fuses failed (I/O error).";
    emit fusesRead(set);
}

void ProgrammerWorker::writeFuses(const FuseSet &fuses)
{
    if (!m_handle || !m_handle->device) {
        emit fuseWriteFinished(false, false, "No chip selected.");
        return;
    }
    if (!fuses.valid || fuses.items.isEmpty()) {
        emit fuseWriteFinished(false, false, "Nothing to write.");
        return;
    }
    if (!m_handle->minipro_begin_transaction || !m_handle->minipro_end_transaction) {
        emit fuseWriteFinished(false, false,
                               "Programmer does not expose the fuse protocol.");
        return;
    }

    const size_t ws = fuses.wordSize ? fuses.wordSize : 1;
    int numFuses = 0, numLocks = 0;
    for (const auto &it : fuses.items) (it.isLock ? numLocks : numFuses)++;

    if (m_handle->minipro_begin_transaction(m_handle)) {
        emit fuseWriteFinished(false, false, "begin_transaction failed");
        return;
    }

    uint8_t wbuf[64] = {0}, vbuf[64] = {0};
    bool ioError = false;
    bool verified = true;

    // Compare a read-back buffer against the requested values, by significant
    // bits — the chip may return unused (out-of-mask) bits differently, so a
    // raw memcmp would falsely fail. Mirrors the minipro CLI's masked compare.
    auto verifySection = [&](bool locks) {
        int idx = 0;
        for (const auto &it : fuses.items) {
            if (it.isLock != locks) continue;
            const quint16 back = static_cast<quint16>(
                load_int(&vbuf[idx * ws], ws, MP_LITTLE_ENDIAN));
            if (fuseSig(back, it.mask) != fuseSig(it.value, it.mask))
                verified = false;
            ++idx;
        }
    };

    // Config-fuse section: write, then read back and compare.
    if (numFuses > 0) {
        int idx = 0;
        for (const auto &it : fuses.items) {
            if (it.isLock) continue;
            format_int(&wbuf[idx * ws], fuseWire(it.value, it.mask), ws,
                       MP_LITTLE_ENDIAN);
            ++idx;
        }
        if (minipro_write_fuses(m_handle, MP_FUSE_CFG, numFuses * ws,
                                static_cast<uint8_t>(numFuses), wbuf)) {
            ioError = true;
        } else if (minipro_read_fuses(m_handle, MP_FUSE_CFG, numFuses * ws,
                                      static_cast<uint8_t>(numFuses), vbuf)) {
            ioError = true;
        } else {
            verifySection(false);
        }
    }

    // Lock section: items_count is word_size. Verify read-back only if readable.
    if (!ioError && numLocks > 0) {
        std::memset(wbuf, 0, sizeof(wbuf));
        int idx = 0;
        for (const auto &it : fuses.items) {
            if (!it.isLock) continue;
            format_int(&wbuf[idx * ws], fuseWire(it.value, it.mask), ws,
                       MP_LITTLE_ENDIAN);
            ++idx;
        }
        if (minipro_write_fuses(m_handle, MP_FUSE_LOCK, numLocks * ws,
                                static_cast<uint8_t>(ws), wbuf)) {
            ioError = true;
        } else if (fuses.lockReadable) {
            std::memset(vbuf, 0, sizeof(vbuf));
            if (minipro_read_fuses(m_handle, MP_FUSE_LOCK, numLocks * ws,
                                   static_cast<uint8_t>(ws), vbuf)) {
                ioError = true;
            } else {
                verifySection(true);
            }
        }
    }

    m_handle->minipro_end_transaction(m_handle);

    if (ioError) {
        emit fuseWriteFinished(false, false, "Writing fuses failed (I/O error).");
        return;
    }
    emit fuseWriteFinished(true, verified,
        verified ? QString()
                 : "Write completed, but read-back verify mismatched.");
}

void ProgrammerWorker::detectChipId()
{
    ChipIdResult r;
    if (!m_handle || !m_handle->device) {
        r.status = ChipIdResult::Status::IoError;
        r.errorMessage = "No chip selected.";
        emit chipIdFinished(r);
        return;
    }
    device_t *dev = m_handle->device;
    if (!dev->flags.has_chip_id) {
        r.status = ChipIdResult::Status::Unsupported;
        emit chipIdFinished(r);
        return;
    }
    if (!m_handle->minipro_begin_transaction
        || !m_handle->minipro_get_chip_id
        || !m_handle->minipro_end_transaction) {
        r.status = ChipIdResult::Status::IoError;
        r.errorMessage = "Programmer does not expose chip-ID protocol.";
        emit chipIdFinished(r);
        return;
    }
    if (m_handle->minipro_begin_transaction(m_handle)) {
        r.status = ChipIdResult::Status::IoError;
        r.errorMessage = "begin_transaction failed";
        emit chipIdFinished(r);
        return;
    }
    uint8_t idType = 0;
    uint32_t readId = 0;
    const int rc = m_handle->minipro_get_chip_id(m_handle, &idType, &readId);
    m_handle->minipro_end_transaction(m_handle);
    if (rc) {
        r.status = ChipIdResult::Status::IoError;
        r.errorMessage = "get_chip_id failed";
        emit chipIdFinished(r);
        return;
    }
    r.expectedId = dev->chip_id;
    r.readId = readId;
    r.idType = idType;
    r.idBytesCount = dev->chip_id_bytes_count;

    quint8 shift = 0;
    bool match = false;
    switch (idType) {
    case MP_ID_TYPE1:
    case MP_ID_TYPE2:
    case MP_ID_TYPE5:
        match = (readId == dev->chip_id);
        break;
    case MP_ID_TYPE3:
        shift = 5;
        match = ((dev->chip_id >> 5) == (readId >> 5));
        r.revisionBits = 5;
        break;
    case MP_ID_TYPE4: {
        auto *cfg = reinterpret_cast<fuse_decl_t *>(dev->config);
        if (cfg) {
            shift = cfg->rev_bits;
            match = ((dev->chip_id >> shift) == (readId >> shift));
            r.revisionBits = shift;
        }
        break;
    }
    default:
        // Unknown id_type — surface as raw bytes; can't compare reliably.
        match = (readId == dev->chip_id);
        break;
    }

    if (match) {
        r.status = ChipIdResult::Status::Match;
    } else {
        r.status = ChipIdResult::Status::Mismatch;
        // Try to identify the chip from the read ID.
        db_data_t db{};
        const QByteArray ipath = m_infoicPath.toUtf8();
        const QByteArray lpath = m_logicicPath.toUtf8();
        db.infoic_path = m_infoicPath.isEmpty() ? nullptr : ipath.constData();
        db.logicic_path = m_logicicPath.isEmpty() ? nullptr : lpath.constData();
        db.prog_version = m_handle->version;
        db.chip_id = (readId >> shift) << shift;
        db.protocol = dev->protocol_id;
        const char *name = get_device_from_id(&db);
        if (name) {
            r.identifiedAs = QString::fromUtf8(name);
            free(const_cast<char *>(name));
        }
    }
    emit chipIdFinished(r);
}

void ProgrammerWorker::writeMemory(MemArea area, const QByteArray &data,
                                   bool force, bool autoVerify)
{
    m_cancel.store(false);

    WriteResult r;
    r.area = area;
    if (!m_handle || !m_handle->device) {
        r.errorMessage = "No chip selected.";
        emit writeFinished(r);
        return;
    }
    device_t *dev = m_handle->device;
    const quint32 total = areaTotalSize(dev, area);
    if (total == 0) {
        r.errorMessage = QString("Chip has no %1 memory.").arg(areaName(area));
        emit writeFinished(r);
        return;
    }
    if (static_cast<quint32>(data.size()) != total) {
        r.errorMessage = QString(
            "Buffer size %1 bytes does not match chip %2 memory %3 bytes. "
            "Resize or reload before writing.")
                             .arg(data.size()).arg(areaName(area)).arg(total);
        emit writeFinished(r);
        return;
    }
    if (!m_handle->minipro_begin_transaction
        || !m_handle->minipro_end_transaction
        || !m_handle->minipro_write_block
        || !m_handle->minipro_read_block) {
        r.errorMessage = "Programmer does not expose write protocol.";
        emit writeFinished(r);
        return;
    }

    const quint32 blockSize = dev->write_buffer_size
                                  ? dev->write_buffer_size
                                  : 64;
    const quint8 mpType = areaToMp(area);

    if (m_handle->minipro_begin_transaction(m_handle)) {
        r.errorMessage = "begin_transaction failed";
        emit writeFinished(r);
        return;
    }

    // Pre-write presence check — same logic as erase. Skip if force OR if
    // the supplied buffer is itself all 0xFF (writing 0xFF is a no-op so
    // the empty-socket scenario doesn't lose data). The presence probe
    // always reads from code memory because data memory is sometimes
    // populated by external initialisation rather than visible at startup.
    if (!force) {
        bool bufferAllFF = true;
        for (qsizetype i = 0; i < data.size(); ++i)
            if (static_cast<quint8>(data[i]) != 0xFF) {
                bufferAllFF = false; break;
            }
        if (!bufferAllFF) {
            bool allFF = false;
            if (probeFirstBlockAllFF(m_handle, &allFF) && allFF) {
                m_handle->minipro_end_transaction(m_handle);
                r.errorMessage =
                    "Pre-write check: chip's first block reads as all "
                    "0xFF — either it's already blank, or no chip is in "
                    "the socket. Refusing to write.\n\n"
                    "If the chip is seated and you intentionally want to "
                    "write anyway, enable the Force checkbox in the "
                    "Write dialog and try again.";
                emit writeFinished(r);
                return;
            }
        }
    }

    data_set_t ds{};
    ds.data = const_cast<uint8_t *>(
        reinterpret_cast<const uint8_t *>(data.constData()));
    ds.type = mpType;
    ds.size = blockSize;
    ds.init = 1;
    ds.block_count = (total + blockSize - 1) / blockSize;

    const quint32 offset = dev->flags.has_data_offset ? dev->page_size : 0;
    const bool wordOrg = (area == MemArea::Code)
        && (dev->flags.data_org == MP_ORG_WORDS);

    bool writeOk = true;
    qint64 done = 0;
    emit progress(0, total);

    for (uint32_t i = 0; i < ds.block_count; ++i) {
        if (m_cancel.load()) {
            r.errorMessage = "Write cancelled — chip contents may be partial.";
            writeOk = false;
            break;
        }
        ds.address = i * blockSize + offset;
        if (wordOrg)
            ds.address >>= 1;

        // Trim last block to remaining bytes.
        if ((i + 1) * blockSize > total)
            ds.size = total - i * blockSize;
        else
            ds.size = blockSize;

        if (m_handle->minipro_write_block(m_handle, &ds)) {
            r.errorMessage = QString("write_block failed at offset 0x%1")
                                 .arg(i * blockSize, 0, 16);
            writeOk = false;
            break;
        }
        ds.data += ds.size;
        ds.init = 0;
        done += ds.size;
        emit progress(qMin<qint64>(done, total), total);
    }

    if (!writeOk) {
        m_handle->minipro_end_transaction(m_handle);
        emit writeFinished(r);
        return;
    }
    r.bytesWritten = done;

    // Optional auto-verify: stay in transaction, read each block back and
    // compare against the source buffer.
    if (autoVerify) {
        std::vector<uint8_t> verifyBuf(blockSize, 0xFF);
        data_set_t vds{};
        vds.data = verifyBuf.data();
        vds.type = mpType;
        vds.size = blockSize;
        vds.init = 1;
        vds.block_count = (total + blockSize - 1) / blockSize;

        const auto *expectedPtr =
            reinterpret_cast<const uint8_t *>(data.constData());
        qint64 vdone = 0;
        emit progress(0, total);
        for (uint32_t i = 0; i < vds.block_count; ++i) {
            if (m_cancel.load()) {
                r.errorMessage = "Verify cancelled after write.";
                writeOk = false;
                break;
            }
            const quint32 blockByteOffset = i * blockSize;
            vds.address = blockByteOffset + offset;
            if (wordOrg) vds.address >>= 1;
            const quint32 thisLen = qMin<quint32>(blockSize, total - blockByteOffset);
            vds.size = thisLen;
            if (m_handle->minipro_read_block(m_handle, &vds)) {
                r.errorMessage = QString("verify read_block failed at 0x%1")
                                     .arg(blockByteOffset, 0, 16);
                writeOk = false;
                break;
            }
            for (quint32 j = 0; j < thisLen; ++j) {
                if (verifyBuf[j] != expectedPtr[blockByteOffset + j])
                    ++r.verifyMismatches;
            }
            vds.init = 0;
            vdone += thisLen;
            emit progress(qMin<qint64>(vdone, total), total);
        }
        r.verified = writeOk && r.verifyMismatches == 0;
    }

    m_handle->minipro_end_transaction(m_handle);
    r.ok = writeOk && (!autoVerify || r.verified);
    if (!r.ok && r.errorMessage.isEmpty() && autoVerify) {
        r.errorMessage = QString("Verify after write: %1 byte%2 differ.")
                             .arg(r.verifyMismatches)
                             .arg(r.verifyMismatches == 1 ? "" : "s");
    }
    emit writeFinished(r);
}
