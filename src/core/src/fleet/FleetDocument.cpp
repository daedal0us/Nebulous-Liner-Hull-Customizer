#include "nlhc/fleet/FleetDocument.h"

#include <QColor>
#include <QDir>
#include <QDomDocument>
#include <QDomElement>
#include <QDomNamedNodeMap>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QVector3D>

#include <utility>

namespace nlhc {
namespace {

constexpr int kMaxPlayableShips = 10;
constexpr int kEditorFleetVersion = 3;
constexpr char kXsiNamespace[] = "http://www.w3.org/2001/XMLSchema-instance";
constexpr char kRandomHullConfiguration[] = "RandomHullConfiguration";

QDomElement childElement(const QDomElement &parent, const QString &tag)
{
    if (parent.isNull())
        return QDomElement();
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isElement() && node.toElement().tagName() == tag)
            return node.toElement();
    }
    return QDomElement();
}

QVector<QDomElement> childElements(const QDomElement &parent, const QString &tag)
{
    QVector<QDomElement> result;
    if (parent.isNull())
        return result;
    for (QDomNode node = parent.firstChild(); !node.isNull(); node = node.nextSibling()) {
        if (node.isElement() && node.toElement().tagName() == tag)
            result.append(node.toElement());
    }
    return result;
}

QString childText(const QDomElement &parent, const QString &tag)
{
    const QDomElement element = childElement(parent, tag);
    return element.isNull() ? QString() : element.text().trimmed();
}

int childInt(const QDomElement &parent, const QString &tag, int fallback)
{
    const QDomElement element = childElement(parent, tag);
    if (element.isNull())
        return fallback;
    bool ok = false;
    const int value = element.text().trimmed().toInt(&ok);
    return ok ? value : fallback;
}

// Copies the fleet beside itself before it is rewritten. A folder that refuses the
// copy (read-only media, a security suite guarding the save folder) must not cost
// the user the save, so the reason is handed back instead of aborting.
bool copyForBackup(const QString &source, const QString &target, QString *reason)
{
    QFile::remove(target);

    QFile from(source);
    if (!from.open(QIODevice::ReadOnly)) {
        *reason = from.errorString();
        return false;
    }

    QFile to(target);
    if (!to.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *reason = to.errorString();
        return false;
    }

    const QByteArray payload = from.readAll();
    if (payload.isEmpty() || to.write(payload) != payload.size() || !to.flush()) {
        *reason = to.errorString();
        to.remove();
        return false;
    }
    return true;
}

float childFloat(const QDomElement &parent, const QString &tag, float fallback)
{
    const QDomElement element = childElement(parent, tag);
    if (element.isNull())
        return fallback;
    bool ok = false;
    const double value = element.text().trimmed().toDouble(&ok);
    return ok ? float(value) : fallback;
}

QString formatReal(float value)
{
    return QString::number(double(value), 'g', 9);
}

QString xsiType(const QDomElement &element)
{
    if (element.isNull())
        return QString();
    if (element.hasAttribute(QStringLiteral("xsi:type")))
        return element.attribute(QStringLiteral("xsi:type"));
    const QDomNamedNodeMap attributes = element.attributes();
    for (int i = 0; i < attributes.count(); ++i) {
        const QDomNode node = attributes.item(i);
        if (!node.isAttr())
            continue;
        const QDomAttr attribute = node.toAttr();
        if (attribute.localName() == QLatin1String("type"))
            return attribute.value();
    }
    return QString();
}

// Creates the child element when missing; keeps an existing one untouched.
QDomElement ensureChild(QDomDocument &document, QDomElement parent, const QString &tag)
{
    QDomElement element = childElement(parent, tag);
    if (!element.isNull())
        return element;
    element = document.createElement(tag);
    parent.appendChild(element);
    return element;
}

bool setElementText(QDomDocument &document, QDomElement element, const QString &text)
{
    if (element.isNull())
        return false;
    if (element.text().trimmed() == text && element.childNodes().count() == 1)
        return false;
    while (!element.firstChild().isNull())
        element.removeChild(element.firstChild());
    element.appendChild(document.createTextNode(text));
    return true;
}

void appendDiagnostic(QVector<Diagnostic> *diagnostics, Severity severity, const QString &code, QString detail,
                      int shipIndex, int segment = -1, int dressingIndex = -1)
{
    if (!diagnostics)
        return;
    Diagnostic diagnostic;
    diagnostic.severity = severity;
    diagnostic.code = code;
    diagnostic.detail = std::move(detail);
    diagnostic.shipIndex = shipIndex;
    diagnostic.segment = segment;
    diagnostic.dressingIndex = dressingIndex;
    diagnostics->append(diagnostic);
}

std::optional<RandomHullConfig> parseHullConfig(const QDomElement &ship, int shipIndex,
                                                QVector<Diagnostic> *diagnostics)
{
    const QDomElement hullConfig = childElement(ship, QStringLiteral("HullConfig"));
    if (hullConfig.isNull()) {
        appendDiagnostic(diagnostics, Severity::Error, QString::fromLatin1(diag::MissingHullConfig), QString(),
                         shipIndex);
        return std::nullopt;
    }
    const QString type = xsiType(hullConfig);
    if (type != QLatin1String(kRandomHullConfiguration)) {
        appendDiagnostic(diagnostics, Severity::Error, QString::fromLatin1(diag::UnsupportedHullConfig),
                         type.isEmpty() ? QStringLiteral("no xsi:type attribute") : type, shipIndex);
        return std::nullopt;
    }

    RandomHullConfig config;

    const QVector<QDomElement> segments =
        childElements(childElement(hullConfig, QStringLiteral("PrimaryStructure")),
                      QStringLiteral("SegmentConfiguration"));
    if (segments.size() != RandomHullConfig::SegmentCount) {
        appendDiagnostic(diagnostics, Severity::Error, QString::fromLatin1(diag::PrimaryStructureSize),
                         QStringLiteral("%1 SegmentConfiguration entries").arg(segments.size()), shipIndex);
    }
    for (int i = 0; i < RandomHullConfig::SegmentCount && i < segments.size(); ++i) {
        const QDomElement &segment = segments.at(i);
        config.primary[size_t(i)].key = childText(segment, QStringLiteral("Key"));
        for (const QDomElement &entry : childElements(childElement(segment, QStringLiteral("Dressing")),
                                                      QStringLiteral("int"))) {
            bool ok = false;
            const int value = entry.text().trimmed().toInt(&ok);
            config.primary[size_t(i)].dressing.append(ok ? value : 0);
        }
    }

    for (const QDomElement &entry : childElements(childElement(hullConfig, QStringLiteral("SecondaryStructure")),
                                                  QStringLiteral("SecondaryStructureConfig"))) {
        SecondaryConfig secondary;
        secondary.key = childText(entry, QStringLiteral("Key"));
        secondary.segment = childInt(entry, QStringLiteral("Segment"), secondary.segment);
        secondary.snap = childInt(entry, QStringLiteral("SnapPoint"), secondary.snap);
        config.secondary.append(secondary);
    }

    const QDomElement tint = childElement(hullConfig, QStringLiteral("HullTint"));
    if (!tint.isNull()) {
        config.hullTint = QColor::fromRgbF(childFloat(tint, QStringLiteral("r"), 0.6f),
                                           childFloat(tint, QStringLiteral("g"), 0.6f),
                                           childFloat(tint, QStringLiteral("b"), 0.6f),
                                           childFloat(tint, QStringLiteral("a"), 1.0f));
    }

    const QDomElement variation = childElement(hullConfig, QStringLiteral("TextureVariation"));
    if (!variation.isNull()) {
        config.textureVariation = QVector3D(childFloat(variation, QStringLiteral("x"), 0.0f),
                                            childFloat(variation, QStringLiteral("y"), 0.0f),
                                            childFloat(variation, QStringLiteral("z"), 0.0f));
    }

    return config;
}

} // namespace

struct FleetDocument::Impl
{
    QDomDocument document;
    QDomElement fleet;
    QVector<QDomElement> ships;
    QString path;
    bool dirty = false;
    bool loaded = false;
    const HullPackage *package = nullptr;

    void reset()
    {
        document = QDomDocument();
        fleet = QDomElement();
        ships.clear();
        path.clear();
        dirty = false;
        loaded = false;
    }
};

FleetDocument::FleetDocument()
    : d(std::make_unique<Impl>())
{
}

FleetDocument::~FleetDocument() = default;

bool FleetDocument::load(const QString &path, QVector<Diagnostic> *diagnostics, QString *error)
{
    const auto fail = [this, error](const QString &message) {
        if (error)
            *error = message;
        d->reset();
        return false;
    };

    QFile file(path);
    if (!file.exists())
        return fail(QStringLiteral("file not found: %1").arg(path));
    if (!file.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("cannot open %1: %2").arg(path, file.errorString()));

    const QByteArray content = file.readAll();
    QDomDocument document;
    const QDomDocument::ParseResult parse = document.setContent(content, QDomDocument::ParseOption::Default);
    if (!parse) {
        return fail(QStringLiteral("%1: XML error at line %2, column %3: %4")
                        .arg(path)
                        .arg(parse.errorLine)
                        .arg(parse.errorColumn)
                        .arg(parse.errorMessage));
    }

    const QDomElement fleet = document.documentElement();
    if (fleet.isNull() || fleet.tagName() != QLatin1String("Fleet"))
        return fail(QStringLiteral("%1: root element must be <Fleet>").arg(path));

    d->reset();
    d->document = document;
    d->fleet = fleet;
    d->ships = childElements(childElement(fleet, QStringLiteral("Ships")), QStringLiteral("Ship"));
    d->path = path;
    d->loaded = true;
    d->dirty = false;

    for (int i = 0; i < d->ships.size(); ++i)
        parseHullConfig(d->ships.at(i), i, diagnostics);

    return true;
}

bool FleetDocument::save(const QString &path, QString *error, QString *notice)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return false;
    };

    if (notice)
        notice->clear();

    if (!d->loaded)
        return fail(QStringLiteral("no document loaded"));
    const QString target = path.isEmpty() ? d->path : path;
    if (target.isEmpty())
        return fail(QStringLiteral("no target path"));

    if (QFileInfo::exists(target)) {
        QString reason;
        if (!copyForBackup(target, target + QStringLiteral(".bak"), &reason) && notice)
            *notice = reason;
    }

    const QByteArray payload = d->document.toByteArray(2);
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly))
        return fail(QStringLiteral("cannot write %1: %2").arg(target, file.errorString()));
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return fail(QStringLiteral("cannot write %1: %2").arg(target, file.errorString()));
    }
    if (!file.commit())
        return fail(QStringLiteral("cannot replace %1: %2").arg(target, file.errorString()));

    d->path = target;
    d->dirty = false;
    return true;
}

bool FleetDocument::isDirty() const
{
    return d->dirty;
}

QString FleetDocument::filePath() const
{
    return d->path;
}

QString FleetDocument::fleetName() const
{
    return childText(d->fleet, QStringLiteral("Name"));
}

QString FleetDocument::factionKey() const
{
    return childText(d->fleet, QStringLiteral("FactionKey"));
}

int FleetDocument::fleetVersion() const
{
    if (!d->loaded)
        return 0;
    return childInt(d->fleet, QStringLiteral("Version"), kEditorFleetVersion);
}

int FleetDocument::shipCount() const
{
    return d->ships.size();
}

void FleetDocument::setPackage(const HullPackage *package)
{
    d->package = package;
}

const HullPackage *FleetDocument::package() const
{
    return d->package;
}

ShipRecord FleetDocument::ship(int index) const
{
    ShipRecord record;
    if (index < 0 || index >= d->ships.size())
        return record;

    const QDomElement element = d->ships.at(index);
    record.index = index;
    record.key = childText(element, QStringLiteral("Key"));
    record.name = childText(element, QStringLiteral("Name"));
    record.hullType = childText(element, QStringLiteral("HullType"));
    record.cost = childInt(element, QStringLiteral("Cost"), 0);
    record.number = childInt(element, QStringLiteral("Number"), 0);
    record.symbolOption = childInt(element, QStringLiteral("SymbolOption"), 0);

    const auto readOnly = [&record](const QString &reason) {
        record.editable = false;
        record.readOnlyReason = reason;
        return record;
    };

    if (index >= kMaxPlayableShips)
        return readOnly(QStringLiteral("The game only loads the first %1 ships of a fleet.").arg(kMaxPlayableShips));
    if (!d->package)
        return readOnly(QStringLiteral("No hull asset package is loaded."));
    if (!d->package->isEditableHullType(record.hullType))
        return readOnly(QStringLiteral("Hull type '%1' is not supported for customization.").arg(record.hullType));
    if (!isRandomHullConfig(index))
        return readOnly(QStringLiteral("This ship has no random hull configuration."));

    record.editable = true;
    return record;
}

bool FleetDocument::isRandomHullConfig(int index) const
{
    if (index < 0 || index >= d->ships.size())
        return false;
    const QDomElement hullConfig = childElement(d->ships.at(index), QStringLiteral("HullConfig"));
    return !hullConfig.isNull() && xsiType(hullConfig) == QLatin1String(kRandomHullConfiguration);
}

std::optional<RandomHullConfig> FleetDocument::hullConfig(int index) const
{
    if (index < 0 || index >= d->ships.size())
        return std::nullopt;
    return parseHullConfig(d->ships.at(index), index, nullptr);
}

bool FleetDocument::setHullConfig(int index, const RandomHullConfig &config)
{
    if (index < 0 || index >= d->ships.size())
        return false;
    const QDomElement shipElement = d->ships.at(index);
    const QDomElement hullConfig = childElement(shipElement, QStringLiteral("HullConfig"));
    if (hullConfig.isNull() || xsiType(hullConfig) != QLatin1String(kRandomHullConfiguration))
        return false;

    const std::optional<RandomHullConfig> current = parseHullConfig(shipElement, index, nullptr);
    if (current.has_value() && *current == config)
        return false;

    QDomDocument &document = d->document;

    QDomElement primary = ensureChild(document, hullConfig, QStringLiteral("PrimaryStructure"));
    QVector<QDomElement> segments = childElements(primary, QStringLiteral("SegmentConfiguration"));
    while (segments.size() < RandomHullConfig::SegmentCount) {
        QDomElement segment = document.createElement(QStringLiteral("SegmentConfiguration"));
        primary.appendChild(segment);
        segments.append(segment);
    }

    for (int i = 0; i < RandomHullConfig::SegmentCount; ++i) {
        const SegmentConfig &wanted = config.primary[size_t(i)];
        const SegmentConfig &before = current.has_value() ? current->primary[size_t(i)] : SegmentConfig();
        QDomElement segment = segments.at(i);
        if (!current.has_value() || before.key != wanted.key)
            setElementText(document, ensureChild(document, segment, QStringLiteral("Key")), wanted.key);
        if (!current.has_value() || before.dressing != wanted.dressing) {
            QDomElement dressing = childElement(segment, QStringLiteral("Dressing"));
            if (dressing.isNull()) {
                dressing = document.createElement(QStringLiteral("Dressing"));
                segment.appendChild(dressing);
            }
            for (const QDomElement &entry : childElements(dressing, QStringLiteral("int")))
                dressing.removeChild(entry);
            for (int value : wanted.dressing) {
                QDomElement entry = document.createElement(QStringLiteral("int"));
                entry.appendChild(document.createTextNode(QString::number(value)));
                dressing.appendChild(entry);
            }
        }
    }

    if (!current.has_value() || current->secondary != config.secondary) {
        QDomElement secondary = ensureChild(document, hullConfig, QStringLiteral("SecondaryStructure"));
        for (const QDomElement &entry : childElements(secondary, QStringLiteral("SecondaryStructureConfig")))
            secondary.removeChild(entry);
        for (const SecondaryConfig &wanted : config.secondary) {
            QDomElement entry = document.createElement(QStringLiteral("SecondaryStructureConfig"));
            setElementText(document, ensureChild(document, entry, QStringLiteral("Key")), wanted.key);
            setElementText(document, ensureChild(document, entry, QStringLiteral("Segment")),
                           QString::number(wanted.segment));
            setElementText(document, ensureChild(document, entry, QStringLiteral("SnapPoint")),
                           QString::number(wanted.snap));
            secondary.appendChild(entry);
        }
    }

    if (!current.has_value() || current->hullTint != config.hullTint) {
        QDomElement tint = ensureChild(document, hullConfig, QStringLiteral("HullTint"));
        setElementText(document, ensureChild(document, tint, QStringLiteral("r")), formatReal(config.hullTint.redF()));
        setElementText(document, ensureChild(document, tint, QStringLiteral("g")), formatReal(config.hullTint.greenF()));
        setElementText(document, ensureChild(document, tint, QStringLiteral("b")), formatReal(config.hullTint.blueF()));
        setElementText(document, ensureChild(document, tint, QStringLiteral("a")), formatReal(config.hullTint.alphaF()));
    }

    if (!current.has_value() || current->textureVariation != config.textureVariation) {
        QDomElement variation = ensureChild(document, hullConfig, QStringLiteral("TextureVariation"));
        setElementText(document, ensureChild(document, variation, QStringLiteral("x")),
                       formatReal(config.textureVariation.x()));
        setElementText(document, ensureChild(document, variation, QStringLiteral("y")),
                       formatReal(config.textureVariation.y()));
        setElementText(document, ensureChild(document, variation, QStringLiteral("z")),
                       formatReal(config.textureVariation.z()));
    }

    d->dirty = true;
    return true;
}

} // namespace nlhc
