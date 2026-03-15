import QtQuick 6.0
import QtQuick.Window 6.0

Item {
    id: root
    width: 640
    height: 480

    // 特效质量：0-低，1-中，2-高（默认）
    property int quality: 2

    // 标题
    Text {
        id: titleText
        text: "BlastPioneer"
        anchors.top: parent.top
        anchors.topMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        font.pixelSize: 32
        font.bold: true
        color: "#333333"  // 深灰色，适应各种背景
        z: 10
    }

    // 底部 Loading 文本
    Text {
        id: loadingText
        text: "Loading"
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 20
        anchors.horizontalCenter: parent.horizontalCenter
        font.pixelSize: 24
        color: "#666666"
        z: 10
    }

    // 粒子数组
    property var particles: []

    // Canvas 绘图区域（覆盖整个根项，但文本在上层）
    Canvas {
        id: canvas
        anchors.fill: parent
        contextType: "2d"

        onPaint: {
            var ctx = getContext("2d");
            ctx.clearRect(0, 0, width, height);
            ctx.globalCompositeOperation = "lighter"; // 叠加混合，增强效果

            for (var i = 0; i < particles.length; i++) {
                var p = particles[i];
                if (p.life <= 0) continue;

                ctx.save();
                ctx.globalAlpha = p.life / p.maxLife * 0.8;
                ctx.fillStyle = p.color;
                ctx.beginPath();
                ctx.arc(p.x, p.y, p.size, 0, Math.PI * 2);
                ctx.fill();
                ctx.restore();
            }
        }
    }

    // 动画定时器（每16ms约60fps）
    Timer {
        id: animationTimer
        interval: 16
        repeat: true
        onTriggered: {
            var delta = 16; // 固定增量，简化处理
            for (var i = particles.length - 1; i >= 0; i--) {
                var p = particles[i];
                p.x += p.vx * delta;
                p.y += p.vy * delta;
                p.vy += p.gravity * delta;
                p.vx *= p.drag;
                p.vy *= p.drag;
                p.life -= delta;
                if (p.life <= 0) {
                    particles.splice(i, 1);
                }
            }
            canvas.requestPaint();
            if (particles.length === 0) {
                animationTimer.stop();
            }
        }
    }

    // 生成单个爆炸点的粒子
    function createExplosionAt(centerX, centerY, count) {
        var newParticles = [];
        for (var i = 0; i < count; i++) {
            var angle = Math.random() * Math.PI * 2;
            var speed = 0.15 + Math.random() * 0.3; // 像素/ms
            var vx = Math.cos(angle) * speed;
            var vy = Math.sin(angle) * speed;
            var life = 2000 + Math.random() * 2000; // 生命2~4秒
            var size = 3 + Math.random() * 8;
            // 随机颜色：火花(橙)、闪光(白)、烟雾(灰)
            var color;
            var r = Math.random();
            if (r < 0.5) color = "#FFAA33";      // 橙黄
            else if (r < 0.8) color = "#FFFFFF"; // 白
            else color = "#808080";               // 灰
            newParticles.push({
                x: centerX, y: centerY,
                vx: vx, vy: vy,
                gravity: 0.00005,
                drag: 0.995,
                life: life,
                maxLife: life,
                size: size,
                color: color
            });
        }
        return newParticles;
    }

    // 爆炸函数：一次生成多个爆炸点
    function explodeMulti(countPoints) {
        // 根据质量决定每个爆炸点的粒子数
        var baseCount = 400; // 高质量
        if (quality === 0) baseCount = 150;
        else if (quality === 1) baseCount = 250;

        // 生成多个爆炸点
        var allParticles = [];
        for (var i = 0; i < countPoints; i++) {
            var x = Math.random() * (root.width - 40) + 20;
            var y = Math.random() * (root.height - 60) + 30; // 避开标题和底部文本
            allParticles = allParticles.concat(createExplosionAt(x, y, baseCount));
        }

        // 合并到主粒子数组
        particles = particles.concat(allParticles);

        // 启动动画循环
        if (!animationTimer.running) {
            animationTimer.start();
        }
    }

    // 自动轮播：每5秒触发一次，每次随机2~4个爆炸点
    Timer {
        interval: 5000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: {
            var pointCount = Math.floor(Math.random() * 3) + 2; // 2~4个
            explodeMulti(pointCount);
        }
    }

    // 鼠标点击手动触发（可测试）
    MouseArea {
        anchors.fill: parent
        onClicked: explodeMulti(3); // 点击一次生成3个爆炸点
    }
}
