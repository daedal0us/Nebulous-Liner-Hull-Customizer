<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>Diagnostic</name>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="12"/>
        <source>bow</source>
        <translation>船艏</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="14"/>
        <source>core</source>
        <translation>船体</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="16"/>
        <source>stern</source>
        <translation>船艉</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="30"/>
        <source>This hull type cannot be customized.</source>
        <translation>该船型不支持定制。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="32"/>
        <source>The asset package does not contain this hull type.</source>
        <translation>资产包中没有该船型。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="34"/>
        <source>The ship has no hull configuration; the game will randomize it.</source>
        <translation>该舰没有船体配置，游戏会将其随机化。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="36"/>
        <source>The hull configuration type is not supported; the game will randomize it.</source>
        <translation>船体配置类型不受支持，游戏会将其随机化。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="38"/>
        <source>The hull needs exactly three primary segments (bow, core, stern).</source>
        <translation>船体主结构必须正好三段（船艏/船体/船艉）。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="40"/>
        <source>A %1 segment is unknown to the asset package; the game would randomize it.</source>
        <translation>第 %1 段在资产包中不存在，游戏会随机替换它。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="43"/>
        <source>The %1 segment lists %2 dressing values but has %3 dressing points.</source>
        <translation>第 %1 段列出了 %2 个装饰值，但该段只有 %3 个装饰点。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="47"/>
        <source>The %1 segment dressing #%2 selects a variant that does not exist.</source>
        <translation>第 %1 段第 %2 个装饰选择了一个不存在的变体。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="51"/>
        <source>A superstructure is unknown to the asset package; the game would drop it.</source>
        <translation>某上层建筑在资产包中不存在，游戏会丢弃它。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="53"/>
        <source>A superstructure targets a segment that does not exist; the game would crash.</source>
        <translation>某上层建筑指向不存在的区段，游戏会崩溃。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="55"/>
        <source>A superstructure targets a snap point that does not exist; the game would crash.</source>
        <translation>某上层建筑指向不存在的挂载点，游戏会崩溃。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="57"/>
        <source>The hull asset package is missing.</source>
        <translation>缺少船体资产包。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="59"/>
        <source>The hull asset package could not be read.</source>
        <translation>船体资产包无法读取。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="61"/>
        <source>A hull mesh is missing from the asset package.</source>
        <translation>资产包中缺少某个船体网格。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="63"/>
        <source>A hull texture is missing from the asset package.</source>
        <translation>资产包中缺少某张贴图。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="70"/>
        <source>The game only loads the first ten ships of a fleet.</source>
        <translation>游戏只会载入舰队中的前 10 艘舰船。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="72"/>
        <source>No hull asset package is loaded.</source>
        <translation>未加载船体资产包。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="74"/>
        <source>Hull type &apos;%1&apos; is not supported for customization.</source>
        <translation>船型 %1 不支持定制。</translation>
    </message>
    <message>
        <location filename="../src/app/DiagnosticText.cpp" line="75"/>
        <source>This ship has no random hull configuration.</source>
        <translation>该舰没有随机船体配置。</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="20"/>
        <source>Bow</source>
        <translation>船艏</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="22"/>
        <source>Core</source>
        <translation>船体</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="24"/>
        <source>Stern</source>
        <translation>船艉</translation>
    </message>
</context>
<context>
    <name>nlhc::HullEditorPanel</name>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="46"/>
        <source>Primary structure</source>
        <translation>主结构</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="65"/>
        <source>Dressing</source>
        <translation>装饰</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="80"/>
        <source>Superstructure</source>
        <translation>上层建筑</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="84"/>
        <source>Add superstructure</source>
        <translation>添加上层建筑</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="180"/>
        <source>(empty)</source>
        <translation>（空）</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="189"/>
        <location filename="../src/app/HullEditorPanel.cpp" line="264"/>
        <source>invalid (%1)</source>
        <translation>无效（%1）</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="214"/>
        <source>The selected pieces have no dressing points.</source>
        <translation>所选区段没有装饰点。</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="242"/>
        <source>Remove</source>
        <translation>移除</translation>
    </message>
    <message>
        <location filename="../src/app/HullEditorPanel.cpp" line="247"/>
        <location filename="../src/app/HullEditorPanel.cpp" line="320"/>
        <source>unknown (%1)</source>
        <translation>未知（%1）</translation>
    </message>
</context>
<context>
    <name>nlhc::MainWindow</name>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="64"/>
        <source>Asset package not found. Reinstall the application or restore assets/hulls.</source>
        <translation>未找到资产包，请重新安装程序或恢复 assets/hulls。</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="107"/>
        <location filename="../src/app/MainWindow.cpp" line="289"/>
        <source>Filter ships</source>
        <translation>筛选舰船</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="266"/>
        <source>&amp;File</source>
        <translation>文件(&amp;F)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="267"/>
        <source>&amp;Open fleet...</source>
        <translation>打开舰队(&amp;O)…</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="268"/>
        <source>&amp;Save</source>
        <translation>保存(&amp;S)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="269"/>
        <source>Save &amp;as...</source>
        <translation>另存为(&amp;A)…</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="270"/>
        <source>Recent</source>
        <translation>最近打开</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="271"/>
        <source>Export view as PNG...</source>
        <translation>导出视图为 PNG…</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="272"/>
        <source>E&amp;xit</source>
        <translation>退出(&amp;X)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="273"/>
        <source>&amp;Edit</source>
        <translation>编辑(&amp;E)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="274"/>
        <source>&amp;Randomize hull</source>
        <translation>随机船体(&amp;R)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="275"/>
        <source>Randomize whole fleet</source>
        <translation>随机整个舰队</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="276"/>
        <source>Copy hull to same ship type</source>
        <translation>复制船体到同型舰船</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="277"/>
        <source>Repair hull configuration</source>
        <translation>修复船体配置</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="278"/>
        <source>&amp;Language</source>
        <translation>语言(&amp;L)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="279"/>
        <source>&amp;Help</source>
        <translation>帮助(&amp;H)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="280"/>
        <source>&amp;About NLHC</source>
        <translation>关于 NLHC(&amp;A)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="282"/>
        <source>System default</source>
        <translation>跟随系统</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="283"/>
        <source>English</source>
        <translation>English</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="287"/>
        <source>Diagnostics</source>
        <translation>诊断</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="340"/>
        <source>No fleet</source>
        <translation>未打开舰队</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="376"/>
        <source>Open fleet</source>
        <translation>打开舰队</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="377"/>
        <source>Fleet files (*.fleet);;All files (*)</source>
        <translation>舰队文件 (*.fleet);;所有文件 (*)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="388"/>
        <source>Open failed</source>
        <translation>打开失败</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="409"/>
        <location filename="../src/app/MainWindow.cpp" line="428"/>
        <source>Save failed</source>
        <translation>保存失败</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="413"/>
        <source>Saved %1</source>
        <translation>已保存 %1</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="419"/>
        <source>Save fleet as</source>
        <translation>舰队另存为</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="423"/>
        <source>Fleet files (*.fleet)</source>
        <translation>舰队文件 (*.fleet)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="438"/>
        <source>Export view</source>
        <translation>导出视图</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="439"/>
        <source>PNG image (*.png)</source>
        <translation>PNG 图片 (*.png)</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="444"/>
        <source>Export failed</source>
        <translation>导出失败</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="444"/>
        <source>Could not write %1</source>
        <translation>无法写入 %1</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="449"/>
        <source>About NLHC</source>
        <translation>关于 NLHC</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="450"/>
        <source>&lt;b&gt;Nebulous Liner Hull Customizer&lt;/b&gt;&lt;br/&gt;Version %1&lt;br/&gt;&lt;br/&gt;Customizes the modular hull of the Protectorate Bulk Hauler and Container Hauler liners in .fleet files.</source>
        <translation>&lt;b&gt;Nebulous Liner Hull Customizer&lt;/b&gt;&lt;br/&gt;版本 %1&lt;br/&gt;&lt;br/&gt;用于定制 .fleet 文件中 Protectorate 阵营 Bulk Hauler 与 Container Hauler 系列舰船的模块化船体。</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="487"/>
        <source>Unsaved changes</source>
        <translation>未保存的更改</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="488"/>
        <source>Save changes to %1 before continuing?</source>
        <translation>是否在继续前保存对 %1 的更改？</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="587"/>
        <source>No problems detected.</source>
        <translation>未发现问题。</translation>
    </message>
    <message>
        <location filename="../src/app/MainWindow.cpp" line="656"/>
        <source>Applied hull to %1 ship(s).</source>
        <translation>已应用到 %1 艘舰船。</translation>
    </message>
</context>
<context>
    <name>nlhc::ShipListModel</name>
    <message>
        <location filename="../src/app/ShipListModel.cpp" line="86"/>
        <source>Name</source>
        <translation>名称</translation>
    </message>
    <message>
        <location filename="../src/app/ShipListModel.cpp" line="88"/>
        <source>Hull type</source>
        <translation>船型</translation>
    </message>
    <message>
        <location filename="../src/app/ShipListModel.cpp" line="90"/>
        <source>Cost</source>
        <translation>造价</translation>
    </message>
    <message>
        <location filename="../src/app/ShipListModel.cpp" line="92"/>
        <source>Count</source>
        <translation>数量</translation>
    </message>
</context>
<context>
    <name>nlhc::StylePanel</name>
    <message>
        <location filename="../src/app/StylePanel.cpp" line="45"/>
        <source>Re-roll</source>
        <translation>重新随机</translation>
    </message>
    <message>
        <location filename="../src/app/StylePanel.cpp" line="49"/>
        <location filename="../src/app/StylePanel.cpp" line="118"/>
        <source>Base color</source>
        <translation>主色</translation>
    </message>
    <message>
        <location filename="../src/app/StylePanel.cpp" line="51"/>
        <location filename="../src/app/StylePanel.cpp" line="118"/>
        <source>Trim color</source>
        <translation>装饰色</translation>
    </message>
    <message>
        <location filename="../src/app/StylePanel.cpp" line="53"/>
        <source>Texture variation</source>
        <translation>贴图变化</translation>
    </message>
    <message>
        <location filename="../src/app/StylePanel.cpp" line="61"/>
        <source>Hull tint</source>
        <translation>船体色调</translation>
    </message>
    <message>
        <location filename="../src/app/StylePanel.cpp" line="80"/>
        <source>Preview in-game tint</source>
        <translation>按游戏 HullTint 预览</translation>
    </message>
</context>
</TS>
