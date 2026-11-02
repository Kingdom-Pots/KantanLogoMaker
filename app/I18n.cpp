#include "app/I18n.h"
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QTabWidget>
#include <QToolBar>
#include <unordered_map>
#include <vector>

namespace kantan::app {
namespace {
Language g_language = Language::English;

struct Entry {
    std::string_view en;
    const char* zhTW;
    const char* zhCN;
    const char* ja;
    const char* es;
};

constexpr Entry kTranslations[] = {
    // Brand & Window
    {"Kantan Logo Maker", "簡單 Logo Maker", "简单 Logo Maker", "簡単 Logo Maker", "Kantan Logo Maker"},
    {"Untitled logo", "未命名標誌", "未命名标志", "無題のロゴ", "Logo sin título"},
    {"Tools", "工具", "工具", "ツール", "Herramientas"},
    {"Tool options", "工具選項", "工具选项", "ツールオプション", "Opciones de herramienta"},
    {"Tool help", "工具說明", "工具说明", "ツールヘルプ", "Ayuda de herramienta"},

    // Tools
    {"Select (V)", "選取 (V)", "选择 (V)", "選択 (V)", "Seleccionar (V)"},
    {"Direct select / nodes (A)", "直接選取 / 節點 (A)", "直接选择 / 节点 (A)", "ダイレクト選択 / 頂点 (A)",
     "Selección directa / nodos (A)"},
    {"Pen (P)", "鋼筆 (P)", "钢笔 (P)", "ペン (P)", "Pluma (P)"},
    {"Shape (R)", "形狀 (R)", "形状 (R)", "シェイプ (R)", "Forma (R)"},
    {"Ellipse (E)", "橢圓 (E)", "椭圆 (E)", "楕円 (E)", "Elipse (E)"},
    {"Text (T)", "文字 (T)", "文字 (T)", "テキスト (T)", "Texto (T)"},
    {"Place image (I)", "置入圖片 (I)", "置入图片 (I)", "画像を配置 (I)", "Colocar imagen (I)"},
    {"Eyedropper (K, or hold Alt)", "滴管 (K，或按住 Alt)", "吸管 (K，或按住 Alt)",
     "スポイト (K、または Alt 長押し)", "Cuentagotas (K, o mantén Alt)"},
    {"Hand (Space)", "抓手 (空白鍵)", "抓手 (空格键)", "手のひら (Space)", "Mano (Espacio)"},
    {"Zoom (Z)", "縮放 (Z)", "缩放 (Z)", "ズーム (Z)", "Zoom (Z)"},

    // Top-level Menus
    {"File", "檔案", "文件", "ファイル", "Archivo"},
    {"Edit", "編輯", "编辑", "編集", "Edición"},
    {"Object", "物件", "对象", "オブジェクト", "Objeto"},
    {"Text", "文字", "文字", "テキスト", "Texto"},
    {"Path", "路徑", "路径", "パス", "Trazado"},
    {"Effects", "效果", "效果", "エフェクト", "Efectos"},
    {"Arrange", "排列", "排列", "整列・順序", "Organizar"},
    {"View", "檢視", "视图", "表示", "Vista"},
    {"Language", "語言", "语言", "言語", "Idioma"},
    {"Help", "說明", "帮助", "ヘルプ", "Ayuda"},

    // File Menu
    {"New logo…", "新增標誌…", "新建标志…", "新規ロゴ…", "Nuevo logo…"},
    {"New logo", "新增標誌", "新建标志", "新規ロゴ", "Nuevo logo"},
    {"New from template", "從範本新增", "从模板新建", "テンプレートから新規作成", "Nuevo desde plantilla"},
    {"Open…", "開啟…", "打开…", "開く…", "Abrir…"},
    {"Open logo", "開啟標誌", "打开标志", "ロゴを開く", "Abrir logo"},
    {"Open recent", "開啟最近使用的檔案", "打开最近的文件", "最近使ったファイルを開く", "Abrir recientes"},
    {"No recent logos", "無最近使用的標誌", "无最近的标志", "最近のロゴはありません",
     "No hay logos recientes"},
    {"Clear list", "清除清單", "清除列表", "履歴をクリア", "Borrar lista"},
    {"Start screen…", "開始畫面…", "开始屏幕…", "スタート画面…", "Pantalla de inicio…"},
    {"Save", "儲存", "保存", "保存", "Guardar"},
    {"Save as…", "另存新檔…", "另存为…", "名前を付けて保存…", "Guardar como…"},
    {"Save logo", "儲存標誌", "保存标志", "ロゴを保存", "Guardar logo"},
    {"Import SVG…", "匯入 SVG…", "导入 SVG…", "SVG をインポート…", "Importar SVG…"},
    {"Import SVG", "匯入 SVG", "导入 SVG", "SVG をインポート", "Importar SVG"},
    {"Place image…", "置入圖片…", "置入图片…", "画像を配置…", "Colocar imagen…"},
    {"Place image", "置入圖片", "置入图片", "画像を配置", "Colocar imagen"},
    {"Import font…", "匯入字型…", "导入字体…", "フォントをインポート…", "Importar fuente…"},
    {"Import font", "匯入字型", "导入字体", "フォントをインポート", "Importar fuente"},
    {"Export…", "匯出…", "导出…", "書き出し…", "Exportar…"},
    {"Export", "匯出", "导出", "書き出し", "Exportar"},
    {"Export as SVG…", "匯出為 SVG…", "导出为 SVG…", "SVG として書き出し…", "Exportar como SVG…"},
    {"Export as SVG", "匯出為 SVG", "导出为 SVG", "SVG として書き出し", "Exportar como SVG"},
    {"Export SVG…", "匯出 SVG…", "导出 SVG…", "SVG 書き出し…", "Exportar SVG…"},
    {"Export SVG", "匯出 SVG", "导出 SVG", "SVG 書き出し", "Exportar SVG"},
    {"Export again", "再次匯出", "再次导出", "再書き出し", "Exportar de nuevo"},
    {"Export with the last settings, folder and name (Ctrl+E)", "使用上次的設定、資料夾與名稱匯出 (Ctrl+E)",
     "使用上次的设置、文件夹和名称导出 (Ctrl+E)", "前回の設定・フォルダ・名前で書き出し (Ctrl+E)",
     "Exportar con los últimos ajustes, carpeta y nombre (Ctrl+E)"},
    {"Close", "關閉", "关闭", "閉じる", "Cerrar"},

    // Edit Menu
    {"Undo", "復原", "撤销", "元に戻す", "Deshacer"},
    {"Redo", "重做", "重做", "やり直す", "Rehacer"},
    {"Cut", "剪下", "剪切", "切り取り", "Cortar"},
    {"Copy", "複製", "复制", "コピー", "Copiar"},
    {"Paste", "貼上", "粘贴", "貼り付け", "Pegar"},
    {"Duplicate (repeats the last spacing)", "再製（重複上次間距）", "复制副本（重复上次间距）",
     "複製（前回の間隔を繰り返す）", "Duplicar (repite el último espaciado)"},
    {"Duplicate", "再製", "复制副本", "複製", "Duplicar"},
    {"Delete", "刪除", "删除", "削除", "Eliminar"},
    {"Select all", "全選", "全选", "すべて選択", "Seleccionar todo"},
    {"Deselect", "取消選取", "取消选择", "選択を解除", "Deseleccionar"},

    // Object Menu
    {"Group", "群組", "编组", "グループ化", "Agrupar"},
    {"Ungroup", "解散群組", "取消编组", "グループ解除", "Desagrupar"},
    {"Clip to bottom shape", "剪裁至底層形狀", "剪切到底层形状", "最背面のシェイプでクリップ",
     "Recortar con la forma inferior"},
    {"Lock", "鎖定", "锁定", "ロック", "Bloquear"},
    {"Unlock", "解除鎖定", "解锁", "ロック解除", "Desbloquear"},
    {"Unlock all", "全部解除鎖定", "全部解锁", "すべてロック解除", "Desbloquear todo"},
    {"Hide", "隱藏", "隐藏", "非表示", "Ocultar"},
    {"Show", "顯示", "显示", "表示", "Mostrar"},
    {"Show all", "全部顯示", "全部显示", "すべて表示", "Mostrar todo"},
    {"Radial repeat…", "放射狀重複…", "径向重复…", "放射状リピート…", "Repetición radial…"},
    {"Radial repeat", "放射狀重複", "径向重复", "放射状リピート", "Repetición radial"},
    {"Copies around the artboard centre", "圍繞畫板中心的副本數量", "围绕画板中心的副本数量",
     "アートボード中心周りのコピー数", "Copias alrededor del centro de la mesa de trabajo"},
    {"Save selection as preset…", "將選取項目儲存為預設…", "将所选内容保存为预设…",
     "選択オブジェクトをプリセットとして保存…", "Guardar selección como preajuste…"},
    {"Artboard…", "畫板…", "画板…", "アートボード…", "Mesa de trabajo…"},
    {"Artboard", "畫板", "画板", "アートボード", "Mesa de trabajo"},

    // Text Menu
    {"Add text", "新增文字", "添加文字", "テキストを追加", "Añadir texto"},
    {"Project fonts…", "專案字型…", "项目字体…", "プロジェクトフォント…", "Fuentes del proyecto…"},
    {"Project fonts", "專案字型", "项目字体", "プロジェクトフォント", "Fuentes del proyecto"},
    {"Embed", "嵌入", "嵌入", "埋め込む", "Incrustar"},

    // Path Menu & Point Actions
    {"Combine", "合併形狀", "合并形状", "複合シェイプ", "Combinar"},
    {"Union", "聯集", "联集", "合体", "Unir"},
    {"Subtract", "減去頂層", "减去顶层", "前面オブジェクトで型抜き", "Restar"},
    {"Intersect", "交集", "交集", "交差", "Intersecar"},
    {"Exclude", "差集", "差集", "中マド", "Excluir"},
    {"Flatten boolean", "平面化布林運算", "拼合布尔运算", "複合シェイプを拡張", "Aplanar booleano"},
    {"Convert to path", "轉換為路徑", "转换为路径", "パスに変換", "Convertir en trazado"},
    {"Offset path…", "位移路徑…", "偏移路径…", "パスのオフセット…", "Desplazar trazado…"},
    {"Offset path", "位移路徑", "偏移路径", "パスのオフセット", "Desplazar trazado"},
    {"Distance (negative shrinks)", "距離（負值為內縮）", "距离（负值为内缩）", "距離（負の値で縮小）",
     "Distancia (negativo encoge)"},
    {"Outline stroke", "筆畫轉外框", "轮廓化描边", "パスのアウトライン", "Contornear trazado"},
    {"Simplify…", "簡化路徑…", "简化路径…", "パスの単純化…", "Simplificar…"},
    {"Simplify", "簡化路徑", "简化路径", "パスの単純化", "Simplificar"},
    {"Tolerance (units)", "容差（單位）", "容差（单位）", "許容値（単位）", "Tolerancia (unidades)"},
    {"Reverse direction", "反轉路徑方向", "反转路径方向", "パスの方向を反転", "Invertir dirección"},
    {"Add point", "新增節點", "添加节点", "頂点を追加", "Añadir punto"},
    {"Move point up", "上移節點", "上移节点", "頂点を上へ移動", "Mover punto arriba"},
    {"Move point down", "下移節點", "下移节点", "頂点を下へ移動", "Mover punto abajo"},
    {"Delete point", "刪除節點", "删除节点", "頂点を削除", "Eliminar punto"},
    {"Delete node", "刪除節點", "删除节点", "頂点を削除", "Eliminar nodo"},
    {"Break at selected node", "在選取節點處斷開", "在所选节点处断开", "選択した頂点でパスを分割",
     "Romper en el nodo seleccionado"},
    {"Break path", "斷開路徑", "断开路径", "パスを分割", "Romper trazado"},
    {"Close open paths", "封閉開放路徑", "闭合开放路径", "オープンパスを閉じる", "Cerrar trazados abiertos"},
    {"Edit text", "編輯文字", "编辑文字", "テキストを編集", "Editar texto"},

    // Effects Menu
    {"Copy style", "複製樣式", "复制样式", "スタイルをコピー", "Copiar estilo"},
    {"Paste style", "貼上樣式", "粘贴样式", "スタイルを貼り付け", "Pegar estilo"},
    {"Clear effects", "清除效果", "清除效果", "エフェクトを消去", "Borrar efectos"},

    // Arrange Menu
    {"Bring to front", "移至最前", "移至顶层", "最前面へ", "Traer al frente"},
    {"Bring forward", "往前移一層", "上移一层", "前面へ", "Traer adelante"},
    {"Send backward", "往後移一層", "下移一层", "背面へ", "Enviar atrás"},
    {"Send to back", "移至最後", "移至底层", "最背面へ", "Enviar al fondo"},
    {"Align", "對齊", "对齐", "整列", "Alinear"},
    {"Align to", "對齊至", "对齐到", "整列基準", "Alinear a"},
    {"Selection", "選取範圍", "所选内容", "選択範囲", "Selección"},
    {"Canvas (as arrangement)", "畫布（整體排列）", "画布（整体排列）", "キャンバス（配置を維持）",
     "Lienzo (como conjunto)"},
    {"Canvas (each object)", "畫布（個別物件）", "画布（单个对象）", "キャンバス（各オブジェクト）",
     "Lienzo (cada objeto)"},
    {"Align left", "靠左對齊", "左对齐", "左揃え", "Alinear a la izquierda"},
    {"Centre horizontally", "水平置中", "水平居中", "水平方向中央揃え", "Centrar horizontalmente"},
    {"Align right", "靠右對齊", "右对齐", "右揃え", "Alinear a la derecha"},
    {"Align top", "靠上對齊", "顶对齐", "上揃え", "Alinear arriba"},
    {"Centre vertically", "垂直置中", "垂直居中", "垂直方向中央揃え", "Centrar verticalmente"},
    {"Align bottom", "靠下對齊", "底对齐", "下揃え", "Alinear abajo"},
    {"Distribute horizontally", "水平均分", "水平分布", "水平方向に等間隔に分布",
     "Distribuir horizontalmente"},
    {"Distribute vertically", "垂直均分", "垂直分布", "垂直方向に等間隔に分布", "Distribuir verticalmente"},
    {"Left", "靠左", "左", "左", "Izquierda"},
    {"Centre", "置中", "居中", "中央", "Centro"},
    {"Right", "靠右", "右", "右", "Derecha"},
    {"Top", "靠上", "顶", "上", "Arriba"},
    {"Middle", "垂直置中", "垂直居中", "上下中央", "Medio"},
    {"Bottom", "靠下", "底", "下", "Abajo"},
    {"Flip horizontal", "水平翻轉", "水平翻转", "水平方向に反転", "Voltear horizontalmente"},
    {"Flip vertical", "垂直翻轉", "垂直翻转", "垂直方向に反転", "Voltear verticalmente"},

    // View Menu
    {"Zoom in", "放大", "放大", "ズームイン", "Acercar"},
    {"Zoom out", "縮小", "缩小", "ズームアウト", "Alejar"},
    {"Fit artboard", "適合畫板大小", "适合画板", "アートボード全体を表示", "Ajustar a la mesa de trabajo"},
    {"Actual size", "實際大小", "实际大小", "実際のサイズ", "Tamaño real"},
    {"Zoom to selection", "縮放至選取範圍", "缩放到所选内容", "選択範囲にズーム", "Zoom a la selección"},
    {"Reset workspace layout", "重設工作區版面", "重置工作区布局", "ワークスペースレイアウトをリセット",
     "Restablecer diseño del espacio de trabajo"},
    {"Snapping", "貼齊", "对齐吸附", "スナップ", "Ajuste magnético"},
    {"Snap", "啟用貼齊", "启用吸附", "スナップを有効化", "Ajustar"},
    {"Whole units", "整數單位", "整数单位", "整数単位", "Unidades enteras"},
    {"Artboard edges and centre", "畫板邊緣與中心", "画板边缘与中心", "アートボードの端と中心",
     "Bordes y centro de la mesa de trabajo"},
    {"Other objects", "其他物件與節點", "其他对象与节点", "他のオブジェクト・頂点", "Otros objetos y puntos"},
    {"Guides", "參考線", "参考线", "ガイド", "Guías"},
    {"Preview on…", "實景預覽…", "实景预览…", "プレビュー表示…", "Vista previa en…"},
    {"Safe zone", "安全區域", "安全区域", "セーフゾーン", "Zona segura"},
    {"None", "無", "无", "なし", "Ninguno"},
    {"Circle (avatars, round icons)", "圓形（頭像、圓形圖示）", "圆形（头像、圆形图标）",
     "円形（アバター・丸アイコン）", "Círculo (avatares, iconos redondos)"},
    {"Rounded square (iOS / macOS mask)", "圓角方形（iOS / macOS 遮罩）", "圆角方形（iOS / macOS 遮罩）",
     "角丸正方形（iOS / macOS マスク）", "Cuadrado redondeado (máscara iOS / macOS)"},
    {"Android adaptive (72 dp visible, 66 dp safe)", "Android 自動調整圖示（72 dp 可見，66 dp 安全）",
     "Android 自适应图标（72 dp 可见，66 dp 安全）", "Android アダプティブ（72 dp 表示、66 dp 安全）",
     "Adaptativo de Android (72 dp visible, 66 dp seguro)"},
    {"Add centre guides", "新增中心參考線", "添加中心参考线", "中心ガイドを追加", "Añadir guías centrales"},
    {"Add guide…", "新增參考線…", "添加参考线…", "ガイドを追加…", "Añadir guía…"},
    {"Add guide", "新增參考線", "添加参考线", "ガイドを追加", "Añadir guía"},
    {"Clear guides", "清除參考線", "清除参考线", "ガイドを消去", "Borrar guías"},

    // Help Menu
    {"Keyboard shortcuts", "鍵盤快速鍵", "键盘快捷键", "キーボードショートカット", "Atajos de teclado"},
    {"About Kantan Logo Maker", "關於 簡單 Logo Maker", "关于 简单 Logo Maker", "簡単 Logo Maker について",
     "Acerca de Kantan Logo Maker"},

    // Docks & Panels
    {"Layers", "圖層", "图层", "レイヤー", "Capas"},
    {"Inspector", "屬性檢視器", "属性检查器", "インスペクタ", "Inspector"},
    {"Presets", "預設庫", "预设库", "プリセット", "Preajustes"},
    {"Size preview", "尺寸預覽", "尺寸预览", "サイズプレビュー", "Vista previa de tamaños"},
    {"Find objects by name or type", "依名稱或類型尋找物件", "按名称或类型查找对象",
     "名前または種類でオブジェクトを検索", "Buscar objetos por nombre o tipo"},
    {"Actions", "動作", "操作", "アクション", "Acciones"},
    {"Move layers up", "上移圖層", "上移图层", "レイヤーを前面へ移動", "Subir capas"},
    {"Move layers down", "下移圖層", "下移图层", "レイヤーを背面へ移動", "Bajar capas"},
    {"Shapes", "形狀", "形状", "シェイプ", "Formas"},
    {"Styles", "樣式", "样式", "スタイル", "Estilos"},
    {"Palette", "調色盤", "调色板", "パレット", "Paleta"},
    {"Starter", "範本", "模板", "テンプレート", "Plantillas"},
    {"Search shapes, styles and templates", "搜尋形狀、樣式與範本", "搜索形状、样式和模板",
     "シェイプ・スタイル・テンプレートを検索", "Buscar formas, estilos y plantillas"},

    // Inspector Sections & Labels
    {"Select an object to edit its properties.\nDraw with the Shape (R), Ellipse (E), Pen (P) or Text (T) "
     "tools.",
     "選取物件以編輯其屬性。\n可使用形狀 (R)、橢圓 (E)、鋼筆 (P) 或文字 (T) 工具繪製。",
     "选择一个对象以编辑其属性。\n可使用形状 (R)、椭圆 (E)、钢笔 (P) 或文字 (T) 工具绘制。",
     "オブジェクトを選択してプロパティを編集します。\nシェイプ (R)、楕円 (E)、ペン (P)、テキスト (T) "
     "ツールで描画できます。",
     "Selecciona un objeto para editar sus propiedades.\nDibuja con las herramientas Forma (R), Elipse (E), "
     "Pluma (P) o Texto (T)."},
    {"Unlock the selected objects to edit their properties.", "解除鎖定選取的物件以編輯其屬性。",
     "解锁所选对象以编辑其属性。", "選択したオブジェクトのロックを解除してプロパティを編集します。",
     "Desbloquea los objetos seleccionados para editar sus propiedades."},
    {"Transform", "變形", "变换", "変形", "Transformar"},
    {"Position", "位置", "位置", "位置", "Posición"},
    {"Size", "尺寸", "大小", "サイズ", "Tamaño"},
    {"Angle", "角度", "角度", "角度", "Ángulo"},
    {"Canvas", "畫布對齊", "画布对齐", "キャンバス整列", "Lienzo"},
    {"Point", "節點", "节点", "頂点 (ポイント)", "Punto"},
    {"Points", "節點清單", "节点列表", "頂点リスト", "Puntos"},
    {"Move up", "上移", "上移", "上へ", "Subir"},
    {"Move down", "下移", "下移", "下へ", "Bajar"},
    {"Type", "類型", "类型", "種類", "Tipo"},
    {"Corner", "尖角", "尖角", "コーナー", "Esquina"},
    {"Smooth", "平滑", "平滑", "スムーズ", "Suave"},
    {"Point X", "節點 X", "节点 X", "頂点 X", "Punto X"},
    {"Point Y", "節點 Y", "节点 Y", "頂点 Y", "Punto Y"},
    {"Bevel / radius", "倒角 / 半徑", "倒角 / 半径", "ベベル / 半径", "Bisel / radio"},
    {"Bevel style", "倒角樣式", "倒角样式", "ベベルスタイル", "Estilo de bisel"},
    {"Round", "圓角", "圆角", "丸角", "Redondeado"},
    {"Chamfer", "斜角", "斜角", "面取り", "Chaflán"},
    {"Inverted", "內凹圓角", "内凹圆角", "逆丸角", "Invertido"},
    {"Shape", "形狀", "形状", "シェイプ", "Forma"},
    {"Corner radius", "圓角半徑", "圆角半径", "角丸の半径", "Radio de esquina"},
    {"Link corners", "同步四個角落", "链接四个角", "四隅をリンク", "Vincular esquinas"},
    {"Top right", "右上", "右上", "右上", "Arriba derecha"},
    {"Bottom right", "右下", "右下", "右下", "Abajo derecha"},
    {"Bottom left", "左下", "左下", "左下", "Abajo izquierda"},
    {"Corners", "角落樣式", "角落样式", "角のスタイル", "Esquinas"},
    {"Sides", "邊數", "边数", "辺の数", "Lados"},
    {"Inner radius", "內徑比例", "内径比例", "内径比率", "Radio interior"},
    {"Thickness", "厚度", "厚度", "太さ", "Grosor"},
    {"Start angle", "起始角度", "起始角度", "開始角度", "Ángulo inicial"},
    {"End angle", "結束角度", "结束角度", "終了角度", "Ángulo final"},
    {"Teeth", "齒數", "齿数", "歯の数", "Dientes"},
    {"Tooth depth", "齒深", "齿深", "歯の深さ", "Profundidad de diente"},
    {"Hole", "中心孔徑", "中心孔径", "穴のサイズ", "Agujero"},
    {"Fill rule", "填色規則", "填充规则", "塗りのルール", "Regla de relleno"},
    {"Non-zero", "非零纏繞", "非零环绕", "ノンゼロ", "No cero"},
    {"Even-odd", "奇偶規則", "奇偶规则", "偶奇", "Par-impar"},
    {"All corners", "所有角落", "所有角落", "すべての角", "Todas las esquinas"},
    {"Corner style", "角落樣式", "角落样式", "角のスタイル", "Estilo de esquina"},
    {"Edit points (Convert to path)", "編輯節點（轉換為路徑）", "编辑节点（转换为路径）",
     "頂点を編集（パスに変換）", "Editar puntos (Convertir en trazado)"},
    {"Font", "字型", "字体", "フォント", "Fuente"},
    {"Search fonts…", "搜尋字型…", "搜索字体…", "フォントを検索…", "Buscar fuentes…"},
    {"Weight", "字重", "字重", "ウェイト", "Peso"},
    {"Italic", "斜體", "斜体", "イタリック", "Cursiva"},
    {"All caps", "全部大寫", "全部大写", "すべて大文字", "Todo en mayúsculas"},
    {"Tracking", "字距", "字距", "トラッキング", "Espaciado"},
    {"Line height", "行高", "行高", "行の高さ", "Altura de línea"},
    {"Arc bend", "弧形彎曲", "弧形弯曲", "アーチ湾曲", "Curvatura de arco"},
    {"Image", "圖片", "图片", "画像", "Imagen"},
    {"Brightness", "亮度", "亮度", "明るさ", "Brillo"},
    {"Contrast", "對比度", "对比度", "コントラスト", "Contraste"},
    {"Saturation", "飽和度", "饱和度", "彩度", "Saturación"},
    {"Hue", "色相", "色相", "色相", "Tono"},
    {"Tint", "色調染色", "色调染色", "ティント", "Tinte"},
    {"Tint amount", "染色強度", "染色强度", "ティント量", "Cantidad de tinte"},
    {"Remove colour", "去除背景色", "去除背景色", "背景色を透過", "Eliminar color"},
    {"Key colour", "去背目標色", "抠图目标色", "キーカラー", "Color clave"},
    {"Tolerance", "容差", "容差", "許容値", "Tolerancia"},
    {"Softness", "邊緣柔和度", "边缘柔和度", "ソフトネス", "Suavidad"},
    {"Fill", "填色", "填充", "塗り", "Relleno"},
    {"Solid", "純色", "纯色", "単色", "Sólido"},
    {"Linear gradient", "線性漸層", "线性渐变", "線形グラデーション", "Degradado lineal"},
    {"Radial gradient", "放射漸層", "径向渐变", "放射状グラデーション", "Degradado radial"},
    {"Colour", "顏色", "颜色", "カラー", "Color"},
    {"Start", "起始色", "起始色", "開始色", "Inicio"},
    {"Middle", "中間色", "中间色", "中間色", "Medio"},
    {"End", "結束色", "结束色", "終了色", "Fin"},
    {"Middle stop", "中間色標", "中间色标", "中間分岐点", "Parada intermedia"},
    {"Middle position", "中間位置", "中间位置", "中間位置", "Posición intermedia"},
    {"Centre X", "中心 X", "中心 X", "中心 X", "Centro X"},
    {"Centre Y", "中心 Y", "中心 Y", "中心 Y", "Centro Y"},
    {"Radius", "半徑", "半径", "半径", "Radio"},
    {"Outlines", "外框筆畫", "轮廓描边", "アウトライン", "Contornos"},
    {"Outline 1", "外框 1", "轮廓 1", "アウトライン 1", "Contorno 1"},
    {"Outline 2", "外框 2", "轮廓 2", "アウトライン 2", "Contorno 2"},
    {"Outline 3", "外框 3", "轮廓 3", "アウトライン 3", "Contorno 3"},
    {"  Width", "  寬度", "  宽度", "  太さ", "  Anchura"},
    {"  Colour", "  顏色", "  颜色", "  カラー", "  Color"},
    {"  Position", "  位置", "  位置", "  位置", "  Posición"},
    {"  Join", "  轉角連接", "  转角连接", "  角の形状", "  Unión"},
    {"Outside", "外部", "外部", "外側", "Exterior"},
    {"Inside", "內部", "内部", "内側", "Interior"},
    {"Miter", "尖角", "尖角", "マイター", "Inglete"},
    {"Bevel", "斜角", "斜角", "ベベル", "Bisel"},
    {"Drop shadow", "投影", "投影", "ドロップシャドウ", "Sombra paralela"},
    {"Outer glow", "外發光", "外发光", "光彩（外側）", "Resplandor exterior"},
    {"Inner shadow", "內陰影", "内阴影", "シャドウ（内側）", "Sombra interior"},
    {"Inner glow", "內發光", "内发光", "光彩（内側）", "Resplandor interior"},
    {"Bevel & emboss", "斜角與浮雕", "斜面与浮雕", "ベベルとエンボス", "Bisel y relieve"},
    {"  Angle", "  角度", "  角度", "  角度", "  Ángulo"},
    {"  Distance", "  距離", "  距离", "  距離", "  Distancia"},
    {"  Blur", "  模糊", "  模糊", "  ぼかし", "  Desenfoque"},
    {"  Spread", "  擴展", "  扩展", "  スプレッド", "  Extensión"},
    {"  Opacity", "  不透明度", "  不透明度", "  不透明度", "  Opacidad"},
    {"  Size", "  大小", "  大小", "  サイズ", "  Tamaño"},
    {"  Style", "  樣式", "  样式", "  スタイル", "  Estilo"},
    {"  Profile", "  輪廓曲線", "  轮廓曲线", "  プロファイル", "  Perfil"},
    {"  Depth", "  深度", "  深度", "  深さ", "  Profundidad"},
    {"  Softness", "  柔和度", "  柔和度", "  ソフトネス", "  Suavidad"},
    {"  Light angle", "  光源角度", "  光源角度", "  光源の角度", "  Ángulo de luz"},
    {"  Altitude", "  光源高度", "  光源高度", "  高度", "  Altitud"},
    {"  Highlight", "  亮部顏色", "  高光颜色", "  ハイライト", "  Resaltado"},
    {"  Highlight %", "  亮部 %", "  高光 %", "  ハイライト %", "  Resaltado %"},
    {"  Shadow", "  暗部顏色", "  阴影颜色", "  シャドウ", "  Sombra"},
    {"  Shadow %", "  暗部 %", "  阴影 %", "  シャドウ %", "  Sombra %"},
    {"Appearance", "外觀", "外观", "アピアランス", "Apariencia"},
    {"Opacity", "不透明度", "不透明度", "不透明度", "Opacidad"},
    {"Blend", "混合模式", "混合模式", "描画モード", "Modo de fusión"},
    {"Normal", "正常", "正常", "通常", "Normal"},
    {"Multiply", "色彩增值", "正片叠底", "乗算", "Multiplicar"},
    {"Screen", "濾色", "滤色", "スクリーン", "Trama"},
    {"Overlay", "覆蓋", "叠加", "オーバーレイ", "Superponer"},
    {"Add", "線性加亮", "线性减淡 (添加)", "加算", "Añadir"},
    {"Hide below", "小於此尺寸時隱藏", "小于此尺寸时隐藏", "最小表示サイズ", "Ocultar por debajo de"},
    {"Hide above", "大於此尺寸時隱藏", "大于此尺寸时隐藏", "最大表示サイズ", "Ocultar por encima de"},
    {"Radial copies", "放射狀副本數", "径向副本数", "放射状コピー数", "Copias radiales"},
    {"  Pivot X", "  中心點 X", "  中心点 X", "  中心点 X", "  Pivote X"},
    {"  Pivot Y", "  中心點 Y", "  中心点 Y", "  中心点 Y", "  Pivote Y"},

    // Dialogs (Start, Artboard, Export, Mockup)
    {"New blank logo", "新增空白標誌", "新建空白标志", "空白のロゴを作成", "Nuevo logo en blanco"},
    {"Custom size", "自訂尺寸", "自定义尺寸", "カスタムサイズ", "Tamaño personalizado"},
    {"Create", "建立", "创建", "作成", "Crear"},
    {"Start from a template", "從範本開始", "从模板开始", "テンプレートから開始",
     "Empezar desde una plantilla"},
    {"Recent", "最近使用", "最近使用", "最近のファイル", "Recientes"},
    {"No recent logos yet", "尚無最近使用的標誌", "暂无最近使用的标志", "最近のロゴはまだありません",
     "Aún no hay logos recientes"},
    {"Show this at startup", "啟動時顯示此畫面", "启动时显示此窗口", "起動時にこの画面を表示",
     "Mostrar esto al iniciar"},
    {"Skip", "略過", "跳过", "スキップ", "Omitir"},
    {"Width", "寬度", "宽度", "幅", "Anchura"},
    {"Height", "高度", "高度", "高さ", "Altura"},
    {"Transparent", "透明背景", "透明背景", "透明", "Transparente"},
    {"Background", "背景", "背景", "背景", "Fondo"},
    {"Keep artwork centred", "保持圖稿置中", "保持图稿居中", "アートワークを中央に維持",
     "Mantener el diseño centrado"},
    {"Preset", "預設配置", "预设配置", "プリセット", "Preajuste"},
    {"Custom", "自訂", "自定义", "カスタム", "Personalizado"},
    {"Formats and sizes", "格式與尺寸", "格式与尺寸", "フォーマットとサイズ", "Formatos y tamaños"},
    {"Options", "選項", "选项", "オプション", "Opciones"},
    {"Padding", "內距留白", "内边距留白", "余白 (パディング)", "Margen interior"},
    {"Non-square fit", "非正方形調整", "非正方形适配", "非正方形のフィット", "Ajuste no cuadrado"},
    {"Contain (centred)", "完整包含（置中）", "完整包含（居中）", "全体を含める（中央）",
     "Contener (centrado)"},
    {"Cover", "填滿裁切", "填满裁切", "カバー（切り抜き）", "Cubrir"},
    {"Selection bounds only", "僅匯出選取範圍", "仅导出所选范围", "選択範囲のみ",
     "Solo límites de la selección"},
    {"Source", "來源", "来源", "ソース", "Origen"},
    {"Name", "名稱", "名称", "名前", "Nombre"},
    {"PNG file names", "PNG 檔名格式", "PNG 文件名格式", "PNG ファイル名", "Nombres de archivo PNG"},
    {"Folder", "資料夾", "文件夹", "フォルダ", "Carpeta"},
    {"Browse…", "瀏覽…", "浏览…", "参照…", "Examinar…"},
    {"Preview (1:1)", "預覽 (1:1)", "预览 (1:1)", "プレビュー (1:1)", "Vista previa (1:1)"},
    {"Dark theme", "深色主題", "深色主题", "ダークテーマ", "Tema oscuro"},
    {"Light theme", "淺色主題", "浅色主题", "ライトテーマ", "Tema claro"},

    // Hover & Canvas Tooltips
    {"Double-click to expose points to edit", "連按兩下以顯示並編輯節點", "双击以显示并编辑节点",
     "ダブルクリックして頂点を表示・編集", "Doble clic para mostrar y editar los puntos"},
    {"Double-click to edit text", "連按兩下以編輯文字", "双击以编辑文字", "ダブルクリックしてテキストを編集",
     "Doble clic para editar el texto"},
    {"Double-click to select inside group", "連按兩下以選取群組內部物件", "双击以选择组内对象",
     "ダブルクリックしてグループ内を選択", "Doble clic para seleccionar dentro del grupo"},
    {"Click a point to edit · Alt-click segment to add point · Del removes selected point",
     "點擊節點進行編輯 · 按住 Alt 點擊線段新增節點 · 按 Del 刪除選取節點",
     "点击节点进行编辑 · 按住 Alt 点击线段添加节点 · 按 Del 删除所选节点",
     "頂点をクリックして編集 · Alt+線分クリックで頂点追加 · Del で選択頂点を削除",
     "Clic en un punto para editar · Alt+clic en segmento para añadir punto · Supr elimina el punto"},
    {"Drag to move point (Shift locks axis) · Double-click to toggle corner/smooth",
     "拖曳以移動節點（按住 Shift 鎖定軸向並對齊）· 連按兩下切換尖角/平滑",
     "拖动以移动节点（按住 Shift 锁定轴向并对齐）· 双击切换尖角/平滑",
     "ドラッグで頂点を移動（Shift で軸固定・整列）· ダブルクリックでコーナー/スムーズ切替",
     "Arrastra para mover el punto (Shift bloquea el eje) · Doble clic alterna esquina/suave"},
    {"Drag handle to adjust curve (Shift snaps angle to 45°)", "拖曳控制柄調整曲線（按住 Shift 鎖定 45° 角）",
     "拖动控制柄调整曲线（按住 Shift 锁定 45° 角）",
     "ハンドルをドラッグして曲線を調整（Shift で 45° 単位に固定）",
     "Arrastra el manejador para ajustar la curva (Shift ajusta a 45°)"},
};
} // namespace

const std::vector<LanguageInfo>& supportedLanguages() {
    static const std::vector<LanguageInfo> langs = {
        {Language::English, "en", "English"},
        {Language::ChineseTraditional, "zh_TW", "繁體中文 (Chinese Traditional)"},
        {Language::ChineseSimplified, "zh_CN", "简体中文 (Chinese Simplified)"},
        {Language::Japanese, "ja", "日本語 (Japanese)"},
        {Language::Spanish, "es_ES", "Español (España)"},
    };
    return langs;
}

Language currentLanguage() {
    return g_language;
}

QString currentLanguageCode() {
    for (const auto& info : supportedLanguages())
        if (info.lang == g_language)
            return QString::fromUtf8(info.code);
    return QStringLiteral("en");
}

void setCurrentLanguage(Language lang) {
    g_language = lang;
    QSettings().setValue("ui/language", currentLanguageCode());
}

void setCurrentLanguageByCode(const QString& code) {
    const QString c = code.trimmed();
    if (c.compare("zh_TW", Qt::CaseInsensitive) == 0 || c.compare("zh-TW", Qt::CaseInsensitive) == 0 ||
        c.compare("zh_HK", Qt::CaseInsensitive) == 0) {
        setCurrentLanguage(Language::ChineseTraditional);
        return;
    }
    if (c.compare("zh_CN", Qt::CaseInsensitive) == 0 || c.compare("zh-CN", Qt::CaseInsensitive) == 0 ||
        c.compare("zh", Qt::CaseInsensitive) == 0) {
        setCurrentLanguage(Language::ChineseSimplified);
        return;
    }
    if (c.startsWith("ja", Qt::CaseInsensitive)) {
        setCurrentLanguage(Language::Japanese);
        return;
    }
    if (c.startsWith("es", Qt::CaseInsensitive)) {
        setCurrentLanguage(Language::Spanish);
        return;
    }
    setCurrentLanguage(Language::English);
}

void loadLanguageFromSettings() {
    const QString saved = QSettings().value("ui/language", QStringLiteral("en")).toString();
    setCurrentLanguageByCode(saved);
}

QString brandAppName() {
    switch (g_language) {
    case Language::ChineseTraditional:
        return QStringLiteral("簡單 Logo Maker");
    case Language::ChineseSimplified:
        return QStringLiteral("简单 Logo Maker");
    case Language::Japanese:
        return QStringLiteral("簡単 Logo Maker");
    case Language::Spanish:
    case Language::English:
    default:
        return QStringLiteral("Kantan Logo Maker");
    }
}

QString trUi(std::string_view english) {
    if (g_language == Language::English)
        return QString::fromUtf8(english.data(), static_cast<qsizetype>(english.size()));
    static const auto table = [] {
        std::unordered_map<std::string_view, const Entry*> map;
        map.reserve(std::size(kTranslations));
        for (const auto& e : kTranslations)
            map.emplace(e.en, &e);
        return map;
    }();
    const auto it = table.find(english);
    if (it == table.end())
        return QString::fromUtf8(english.data(), static_cast<qsizetype>(english.size()));
    const Entry* e = it->second;
    const char* translated = nullptr;
    switch (g_language) {
    case Language::ChineseTraditional:
        translated = e->zhTW;
        break;
    case Language::ChineseSimplified:
        translated = e->zhCN;
        break;
    case Language::Japanese:
        translated = e->ja;
        break;
    case Language::Spanish:
        translated = e->es;
        break;
    case Language::English:
        break;
    }
    if (!translated || !*translated)
        return QString::fromUtf8(english.data(), static_cast<qsizetype>(english.size()));
    return QString::fromUtf8(translated);
}

QString trUi(const char* english) {
    if (!english)
        return {};
    return trUi(std::string_view(english));
}

QString trUi(const QString& english) {
    if (g_language == Language::English)
        return english;
    const QByteArray utf8 = english.toUtf8();
    return trUi(std::string_view(utf8.constData(), static_cast<std::size_t>(utf8.size())));
}

QString aboutDialogText(const QString& version) {
    switch (g_language) {
    case Language::ChineseTraditional:
        return QStringLiteral(
                   "簡單 Logo Maker (Kantan Logo Maker) %1\n"
                   "輕量、快速的標誌與圖示製作工具。\n"
                   "由 Kingdom Pots 使用 Claude Opus、GPT 6.1 Sol 與 Google Gemini 4 製作\n\n"
                   "框架與開放原始碼函式庫：\n"
                   "• Qt 6 (LGPL) — 跨平台桌面使用者介面、視窗與圖形框架。\n"
                   "• Clipper2 (BSL-1.0) — 2D "
                   "多邊形幾何函式庫，用於形狀布林運算（聯集、減去、交集、差集）以及路徑位移與外框擴展。\n"
                   "• nanosvg (zlib) — 輕量級 SVG 向量解析器，用於匯入外部向量圖形與內建符號預設。\n"
                   "• Miniz (MIT) — 高速無損資料壓縮與 ZIP 封存函式庫，用於儲存與讀取 .kantanlogo 專案檔。\n"
                   "• Fluent System Icons (MIT) — Microsoft 開放原始碼圖示集，用於工具列與介面圖示。\n\n"
                   "完整授權條款請參閱 notices 資料夾。")
            .arg(version);
    case Language::ChineseSimplified:
        return QStringLiteral(
                   "简单 Logo Maker (Kantan Logo Maker) %1\n"
                   "轻量、快速的标志与图标制作工具。\n"
                   "由 Kingdom Pots 使用 Claude Opus、GPT 6.1 Sol 和 Google Gemini 4 制作\n\n"
                   "框架与开源库：\n"
                   "• Qt 6 (LGPL) — 跨平台桌面用户界面、窗口与图形框架。\n"
                   "• Clipper2 (BSL-1.0) — 2D "
                   "多边形几何库，用于形状布尔运算（联集、减去、交集、差集）以及路径偏移与轮廓扩展。\n"
                   "• nanosvg (zlib) — 轻量级 SVG 矢量解析器，用于导入外部矢量图形与内置符号预设。\n"
                   "• Miniz (MIT) — 高速无损数据压缩与 ZIP 归档库，用于保存和读取 .kantanlogo 项目文件。\n"
                   "• Fluent System Icons (MIT) — Microsoft 开源图标集，用于工具栏与界面图标。\n\n"
                   "完整许可证文本请参阅 notices 文件夹。")
            .arg(version);
    case Language::Japanese:
        return QStringLiteral(
                   "簡単 Logo Maker (Kantan Logo Maker) %1\n"
                   "軽量で高速なロゴ・アイコン作成ツール。\n"
                   "Kingdom Pots が Claude Opus、GPT 6.1 Sol、Google Gemini 4 を使用して制作\n\n"
                   "フレームワーク＆オープンソースライブラリ：\n"
                   "• Qt 6 (LGPL) — クロスプラットフォームのデスクトップ "
                   "UI・ウィンドウ・グラフィックスフレームワーク。\n"
                   "• Clipper2 (BSL-1.0) — "
                   "図形のブーリアン演算（合体・型抜き・交差・中マド）やパスのオフセット・アウトライン展開を"
                   "行う 2D ポリゴン幾何ライブラリ。\n"
                   "• nanosvg (zlib) — "
                   "ベクターグラフィックスや内蔵シンボルプリセットの読み込みに使用される軽量 SVG パーサー。\n"
                   "• Miniz (MIT) — .kantanlogo "
                   "プロジェクトファイルの保存・読み込みに使用される高速ロスレス圧縮・ZIP ライブラリ。\n"
                   "• Fluent System Icons (MIT) — ツールバーや UI アイコンに使用されている Microsoft "
                   "のオープンソースアイコンセット。\n\n"
                   "ライセンス全文については notices フォルダをご参照ください。")
            .arg(version);
    case Language::Spanish:
        return QStringLiteral(
                   "Kantan Logo Maker %1\n"
                   "Un creador de logos e iconos ligero y rápido.\n"
                   "Producido por Kingdom Pots con Claude Opus, GPT 6.1 Sol y Google Gemini 4\n\n"
                   "Frameworks y bibliotecas de código abierto:\n"
                   "• Qt 6 (LGPL) — Framework multiplataforma de interfaz de usuario, ventanas y gráficos de "
                   "escritorio.\n"
                   "• Clipper2 (BSL-1.0) — Biblioteca de geometría poligonal 2D utilizada para operaciones "
                   "booleanas de formas (Unir, Restar, Intersecar, Excluir) y desplazamiento/contorno de "
                   "trazados.\n"
                   "• nanosvg (zlib) — Analizador SVG ligero utilizado para importar gráficos vectoriales y "
                   "símbolos predefinidos.\n"
                   "• Miniz (MIT) — Biblioteca rápida de compresión sin pérdida y archivos ZIP utilizada "
                   "para guardar y abrir proyectos .kantanlogo.\n"
                   "• Fluent System Icons (MIT) — Conjunto de iconos de código abierto de Microsoft "
                   "utilizado en la barra de herramientas y la interfaz.\n\n"
                   "Consulta la carpeta notices para ver los textos completos de las licencias.")
            .arg(version);
    case Language::English:
    default:
        return QStringLiteral(
                   "Kantan Logo Maker %1\n"
                   "A small, fast logo and icon maker.\n"
                   "Produced by Kingdom Pots using Claude Opus, GPT 6.1 Sol and Google Gemini 4\n\n"
                   "Frameworks & Open-Source Libraries:\n"
                   "• Qt 6 (LGPL) — Cross-platform desktop user interface, windowing and graphics "
                   "framework.\n"
                   "• Clipper2 (BSL-1.0) — 2D polygon geometry library used for boolean shape operations "
                   "(Union, Subtract, Intersect, Exclude) and path offsetting/outlines.\n"
                   "• nanosvg (zlib) — Lightweight SVG parser used to import vector graphics and symbol "
                   "presets.\n"
                   "• Miniz (MIT) — Fast lossless data compression and ZIP archive library used to save and "
                   "load .kantanlogo project files.\n"
                   "• Fluent System Icons (MIT) — Microsoft's open-source icon set used for the toolbar and "
                   "interface icons.\n\n"
                   "See the notices folder for full licence texts.")
            .arg(version);
    }
}

namespace {
QString translateStored(QObject* obj, const char* propName, const QString& current) {
    if (current.isEmpty())
        return current;
    QVariant stored = obj->property(propName);
    if (!stored.isValid()) {
        obj->setProperty(propName, current);
        stored = current;
    }
    return trUi(stored.toString());
}

void retranslateAction(QAction* action) {
    if (!action || action->isSeparator() || action->property("i18nSkip").toBool())
        return;
    const QString text = action->text();
    if (!text.isEmpty())
        action->setText(translateStored(action, "_i18n_text", text));
    const QString tip = action->toolTip();
    if (!tip.isEmpty())
        action->setToolTip(translateStored(action, "_i18n_tip", tip));
    if (auto* subMenu = action->menu()) {
        subMenu->setTitle(translateStored(subMenu, "_i18n_title", subMenu->title()));
        for (auto* subAction : subMenu->actions())
            retranslateAction(subAction);
    }
}
} // namespace

void retranslateWidgetTree(QWidget* root) {
    if (!root)
        return;
    QList<QWidget*> widgets = root->findChildren<QWidget*>();
    widgets.prepend(root);
    for (auto* w : widgets) {
        if (!w || w->property("i18nSkip").toBool())
            continue;
        const QString tip = w->toolTip();
        if (!tip.isEmpty())
            w->setToolTip(translateStored(w, "_i18n_tip", tip));
        if (auto* menuBar = qobject_cast<QMenuBar*>(w)) {
            for (auto* a : menuBar->actions())
                retranslateAction(a);
        } else if (auto* menu = qobject_cast<QMenu*>(w)) {
            menu->setTitle(translateStored(menu, "_i18n_title", menu->title()));
            for (auto* a : menu->actions())
                retranslateAction(a);
        } else if (auto* toolBar = qobject_cast<QToolBar*>(w)) {
            toolBar->setWindowTitle(translateStored(toolBar, "_i18n_title", toolBar->windowTitle()));
            for (auto* a : toolBar->actions())
                retranslateAction(a);
        } else if (auto* dock = qobject_cast<QDockWidget*>(w)) {
            dock->setWindowTitle(translateStored(dock, "_i18n_title", dock->windowTitle()));
            if (auto* toggle = dock->toggleViewAction())
                toggle->setText(dock->windowTitle());
        } else if (auto* group = qobject_cast<QGroupBox*>(w)) {
            group->setTitle(translateStored(group, "_i18n_title", group->title()));
        } else if (auto* tabs = qobject_cast<QTabWidget*>(w)) {
            for (int i = 0; i < tabs->count(); ++i) {
                const QByteArray key = QByteArray("_i18n_tab_") + QByteArray::number(i);
                const QString translated = translateStored(tabs, key.constData(), tabs->tabText(i));
                tabs->setTabText(i, translated);
                if (auto* page = tabs->widget(i); page && page->property("baseTitle").isValid())
                    page->setProperty("baseTitle", translated);
            }
        } else if (auto* label = qobject_cast<QLabel*>(w)) {
            if (!label->text().isEmpty() && !label->text().contains("·"))
                label->setText(translateStored(label, "_i18n_text", label->text()));
        } else if (auto* btn = qobject_cast<QAbstractButton*>(w)) {
            if (!btn->text().isEmpty())
                btn->setText(translateStored(btn, "_i18n_text", btn->text()));
        } else if (auto* edit = qobject_cast<QLineEdit*>(w)) {
            if (!edit->placeholderText().isEmpty())
                edit->setPlaceholderText(translateStored(edit, "_i18n_placeholder", edit->placeholderText()));
        } else if (auto* combo = qobject_cast<QComboBox*>(w)) {
            if (combo->objectName() != "fontFamily" && combo->objectName() != "shapePicker" &&
                combo->objectName() != "exportPreset") {
                for (int i = 0; i < combo->count(); ++i) {
                    const QByteArray key = QByteArray("_i18n_item_") + QByteArray::number(i);
                    combo->setItemText(i, translateStored(combo, key.constData(), combo->itemText(i)));
                }
            }
        }
        for (auto* a : w->actions())
            retranslateAction(a);
    }
}
} // namespace kantan::app
