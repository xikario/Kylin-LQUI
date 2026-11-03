"""Compact settings tokens; no backend/config access and no DPR font multiplication."""
COLORS = {'window': '#F5F5F7', 'sidebar': '#ECEEF2', 'surface': '#FFFFFF',
          'text': '#1D1D1F', 'secondary': '#64676D', 'border': '#D7D9DF',
          'divider': '#E7E8EC', 'accent': '#007AFF', 'selected': '#DCEBFF',
          'selectedText': '#095DB5', 'hover': '#F0F3F8', 'pressed': '#E5EBF3',
          'disabledText': '#8C9098', 'error': '#B42318', 'warning': '#925A00', 'primary': '#0066D6'}
SPACING = (4, 8, 12, 16, 20)
PREVIEW_BYTES = 2 * 1024 * 1024
PREVIEW_PIXELS = 1360 * 800


def px(size, scale=1):
    return max(1, round(size * scale))


def application_scale(font, explicit=None):
    # UKUI's 10pt general font is the nominal 14px design baseline. Font
    # enlargement adjusts the generated stylesheet itself, not just app.font.
    return max(.85, min(3., float(explicit) if explicit is not None else font.pointSizeF() / 10.))


def sidebar_width(width, metrics):
    preferred = 184 if width < 900 else 208
    required = metrics.horizontalAdvance('文件管理与扩展') + 80
    return min(max(preferred, required), max(184, width - 320))


def build_settings_style(scale=1):
    c = COLORS
    return '''
QMainWindow,QWidget#body{background:transparent;color:%(text)s;}
QWidget{font-family:"Noto Sans CJK SC","Noto Sans",sans-serif;font-size:%(body)spx;color:%(text)s;}
QFrame#sidebar{background:%(sidebar)s;border:0;border-right:1px solid %(divider)s;border-bottom-left-radius:11px;}
QWidget#pageCanvas,QScrollArea,QScrollArea>QWidget>QWidget{background:transparent;border:0;}
QLabel{background:transparent;}
QLabel#title{font-size:%(title)spx;font-weight:600;}
QLabel#integratedBrand{font-size:%(body)spx;font-weight:600;}
QLabel#subtitle,QLabel[note="true"],QLabel[secondary="true"]{font-size:%(small)spx;color:%(secondary)s;}
QLabel[section="true"]{font-size:%(section)spx;font-weight:600;}
QLabel[warning="true"]{color:%(warning)s;}
QLabel[error="true"]{color:%(error)s;}
QFrame[card="true"]{background:%(surface)s;border:1px solid %(border)s;border-radius:10px;}
QWidget[settingRow="true"]{background:transparent;border:0;}
QListWidget#navigation{background:transparent;border:0;padding:4px;}
QListWidget#navigation::item{border:0;}
QLineEdit,QComboBox,QPlainTextEdit,QListWidget{background:%(surface)s;border:1px solid %(border)s;border-radius:6px;padding:4px 8px;selection-background-color:%(selected)s;selection-color:%(selectedText)s;}
QLineEdit,QComboBox{min-height:%(control)spx;}
QLineEdit#search{background:#FFFFFF;min-height:22px;padding:3px 8px;border-radius:6px;}
QLineEdit:focus,QComboBox:focus,QPlainTextEdit:focus,QListWidget:focus{border:2px solid %(accent)s;padding:3px 7px;}
QComboBox{padding-right:24px;}QComboBox::drop-down{border:0;width:22px;}
QComboBox QAbstractItemView{background:%(surface)s;selection-background-color:%(selected)s;color:%(text)s;border:0;}
QPushButton,QToolButton{background:%(surface)s;color:%(text)s;border:1px solid %(border)s;border-radius:6px;min-height:%(button)spx;padding:4px 10px;}
QPushButton:hover,QToolButton:hover{background:%(hover)s;}
QPushButton:pressed,QToolButton:pressed{background:%(pressed)s;}
QPushButton:checked{background:%(selected)s;color:%(selectedText)s;border-color:%(accent)s;}
QPushButton:focus,QToolButton:focus{border:2px solid %(accent)s;padding:3px 9px;}
QPushButton[primary="true"]{background:%(primary)s;border-color:%(primary)s;color:white;}
QPushButton[primary="true"]:hover{background:#005BC2;}QPushButton[primary="true"]:pressed{background:#004DA5;}
QPushButton[destructive="true"]{color:%(error)s;}
QPushButton:disabled,QToolButton:disabled{color:%(disabledText)s;background:#F4F4F6;border-color:%(divider)s;}
QToolButton[windowControl="true"]{background:transparent;border:0;padding:0;min-height:30px;}
QToolButton[windowControl="true"]:hover{background:%(pressed)s;}
QToolButton[windowControl="true"]:focus{border:2px solid %(accent)s;padding:0;}
QSlider::groove:horizontal{height:4px;background:%(divider)s;border-radius:2px;}
QSlider::sub-page:horizontal{background:%(accent)s;border-radius:2px;}
QSlider::handle:horizontal{width:16px;margin:-6px 0;background:%(surface)s;border:1px solid %(border)s;border-radius:8px;}
QSlider:focus::handle:horizontal{border:2px solid %(accent)s;}
QScrollBar:vertical{width:8px;background:transparent;margin:2px;}
QScrollBar::handle:vertical{background:#BFC3CC;min-height:28px;border-radius:3px;}
QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}QScrollBar::add-page:vertical,QScrollBar::sub-page:vertical{background:transparent;}
QToolTip,QMenu,QMessageBox{background:%(surface)s;color:%(text)s;border:1px solid %(border)s;border-radius:6px;font-size:%(body)spx;}
QToolTip{padding:8px;}QMenu::item:selected{background:%(selected)s;}
''' % dict(c, body=px(14, scale), title=px(20, scale), small=px(12, scale),
           section=px(13, scale), control=max(22, px(14, scale)+6), button=max(22, px(14, scale)+6))
