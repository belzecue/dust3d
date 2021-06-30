#include "preferences.h"
#include "util.h"

#define MAX_RECENT_FILES            7

Preferences &Preferences::instance()
{
    static Preferences *s_preferences = nullptr;
    if (nullptr == s_preferences) {
        s_preferences = new Preferences;
    }
    return *s_preferences;
}

void Preferences::loadDefault()
{
    m_componentCombineMode = CombineMode::Normal;
    m_partColor = Qt::white;
    m_flatShading = false;
    m_toonShading = false;
    m_toonLine = ToonLine::WithoutLine;
    m_textureSize = 1024;
    m_scriptEnabled = false;
    m_interpolationEnabled = true;
}

Preferences::Preferences()
{
    loadDefault();
    {
        QString value = m_settings.value("componentCombineMode").toString();
        if (!value.isEmpty())
            m_componentCombineMode = CombineModeFromString(value.toUtf8().constData());
    }
    {
        QString value = m_settings.value("partColor").toString();
        if (!value.isEmpty())
            m_partColor = QColor(value);
    }
    {
        QString value = m_settings.value("flatShading").toString();
        if (value.isEmpty())
            m_flatShading = false;
        else
            m_flatShading = isTrueValueString(value);
    }
    {
        QString value = m_settings.value("toonShading").toString();
        if (value.isEmpty())
            m_toonShading = false;
        else
            m_toonShading = isTrueValueString(value);
    }
    {
        QString value = m_settings.value("toonLine").toString();
        if (!value.isEmpty())
            m_toonLine = ToonLineFromString(value.toUtf8().constData());
    }
    {
        QString value = m_settings.value("textureSize").toString();
        if (!value.isEmpty())
            m_textureSize = value.toInt();
    }
    {
        QString value = m_settings.value("scriptEnabled").toString();
        if (value.isEmpty())
            m_scriptEnabled = false;
        else
            m_scriptEnabled = isTrueValueString(value);
    }
    {
        QString value = m_settings.value("interpolationEnabled").toString();
        if (value.isEmpty())
            m_interpolationEnabled = true;
        else
            m_interpolationEnabled = isTrueValueString(value);
    }
}

CombineMode Preferences::componentCombineMode() const
{
    return m_componentCombineMode;
}

const QColor &Preferences::partColor() const
{
    return m_partColor;
}

bool Preferences::flatShading() const
{
    return m_flatShading;
}

bool Preferences::scriptEnabled() const
{
    return m_scriptEnabled;
}

bool Preferences::interpolationEnabled() const
{
    return m_interpolationEnabled;
}

bool Preferences::toonShading() const
{
    return m_toonShading;
}

ToonLine Preferences::toonLine() const
{
    return m_toonLine;
}

int Preferences::textureSize() const
{
    return m_textureSize;
}

void Preferences::setComponentCombineMode(CombineMode mode)
{
    if (m_componentCombineMode == mode)
        return;
    m_componentCombineMode = mode;
    m_settings.setValue("componentCombineMode", CombineModeToString(m_componentCombineMode));
    emit componentCombineModeChanged();
}

void Preferences::setPartColor(const QColor &color)
{
    if (m_partColor == color)
        return;
    m_partColor = color;
    m_settings.setValue("partColor", color.name());
    emit partColorChanged();
}

void Preferences::setFlatShading(bool flatShading)
{
    if (m_flatShading == flatShading)
        return;
    m_flatShading = flatShading;
    m_settings.setValue("flatShading", flatShading ? "true" : "false");
    emit flatShadingChanged();
}

void Preferences::setScriptEnabled(bool enabled)
{
    if (m_scriptEnabled == enabled)
        return;
    m_scriptEnabled = enabled;
    m_settings.setValue("scriptEnabled", enabled ? "true" : "false");
    emit scriptEnabledChanged();
}

void Preferences::setInterpolationEnabled(bool enabled)
{
    if (m_interpolationEnabled == enabled)
        return;
    m_interpolationEnabled = enabled;
    m_settings.setValue("interpolationEnabled", enabled ? "true" : "false");
    emit interpolationEnabledChanged();
}

void Preferences::setToonShading(bool toonShading)
{
    if (m_toonShading == toonShading)
        return;
    m_toonShading = toonShading;
    m_settings.setValue("toonShading", toonShading ? "true" : "false");
    emit toonShadingChanged();
}

void Preferences::setToonLine(ToonLine toonLine)
{
    if (m_toonLine == toonLine)
        return;
    m_toonLine = toonLine;
    m_settings.setValue("toonLine", ToonLineToString(m_toonLine));
    emit toonLineChanged();
}

void Preferences::setTextureSize(int textureSize)
{
    if (m_textureSize == textureSize)
        return;
    m_textureSize = textureSize;
    m_settings.setValue("textureSize", QString::number(m_textureSize));
    emit textureSizeChanged();
}

QSize Preferences::documentWindowSize() const
{
    return m_settings.value("documentWindowSize", QSize()).toSize();
}

void Preferences::setDocumentWindowSize(const QSize& size)
{
    m_settings.setValue("documentWindowSize", size);
}

QStringList Preferences::recentFileList() const
{
    return m_settings.value("recentFileList").toStringList();
}

int Preferences::maxRecentFiles() const
{
    return MAX_RECENT_FILES;
}

void Preferences::setCurrentFile(const QString &fileName)
{
    QStringList files = m_settings.value("recentFileList").toStringList();
    
    files.removeAll(fileName);
    files.prepend(fileName);
    while (files.size() > MAX_RECENT_FILES)
        files.removeLast();
    
    m_settings.setValue("recentFileList", files);
}

void Preferences::reset()
{
    auto files = m_settings.value("recentFileList").toStringList();
    m_settings.clear();
    m_settings.setValue("recentFileList", files);
    
    loadDefault();
    emit componentCombineModeChanged();
    emit partColorChanged();
    emit flatShadingChanged();
    emit toonShadingChanged();
    emit toonLineChanged();
    emit textureSizeChanged();
    emit scriptEnabledChanged();
    emit interpolationEnabledChanged();
}
