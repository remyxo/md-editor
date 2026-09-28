#ifndef MD_EDITOR_SELFTESTRUNNER_H
#define MD_EDITOR_SELFTESTRUNNER_H

class Application;
class QString;

// 无头自测试入口（--selftest）：真实 MainWindow/QWebEngine/转换/导出链路的
// 深冒烟与计时基线，Xvfb 下运行。flag-gated，正常启动路径零影响。
// 子命令：
//   roundtrip --file F --rounds N --out J ：加载→N 轮 WYSIWYG↔源码切换
//     （第 1 轮注入一次源码编辑），断言 MD 序列收敛；结果 JSON 落盘
//   export  --file F --outdir D --out J ：无头导出 html/docx/pdf 三格式并计时，
//     导出设置固定为代码默认值（与用户 QSettings 状态无关），结果 JSON 落盘
//   perf    --file F --out J            ：分段计时基线（Task 5）
class SelfTestRunner {
public:
    static bool isSelfTestInvocation();   // 启动参数含 --selftest
    static int run(Application &app);     // 跑完返回进程退出码（0=成功）
};

#endif   // MD_EDITOR_SELFTESTRUNNER_H
