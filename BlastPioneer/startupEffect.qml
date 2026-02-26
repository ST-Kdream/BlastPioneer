import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Particles 2.15

Window {
    width: 800
    height: 600
    visible: true
    title: "炸弹爆炸粒子特效 - 点击引爆"
    color: "black"

    Item {
        id: root
        anchors.fill: parent

        ParticleSystem {
            id: particleSystem
        }

        // ---------- 粒子外观定义 ----------
        // 1. 火花：炽热、旋转、带有颜色变化
        ImageParticle {
            groups: ["sparks"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#FFAA33"
            colorVariation: 0.8          // 随机颜色变化，产生橙红黄等色调
            alpha: 1.0
            alphaVariation: 0.3           // 透明度随机变化
            entryEffect: ImageParticle.Fade    // 淡入淡出效果
            rotation: 0
            rotationVariation: 360         // 初始旋转角度随机
            rotationVelocity: 180           // 每秒旋转180度
            rotationVelocityVariation: 90   // 旋转速度随机变化
        }

        // 2. 烟雾：缓慢上升、消散、半透明
        ImageParticle {
            groups: ["smoke"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#AAAAAA"
            colorVariation: 0.4
            alpha: 0.7
            alphaVariation: 0.2
            entryEffect: ImageParticle.Fade
        }

        // 3. 冲击波：快速膨胀、半透明青色
        ImageParticle {
            groups: ["shockwave"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#88CCFF"
            colorVariation: 0.2
            alpha: 0.9
            alphaVariation: 0.1
            entryEffect: ImageParticle.Scale   // 缩放效果
        }

        // 4. 闪光：短暂高亮，使用纯白色和快速淡出
        ImageParticle {
            groups: ["flash"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#FFFFFF"
            alpha: 1.0
            entryEffect: ImageParticle.Scale
        }

        // ---------- 粒子发射器 ----------
        Emitter {
            id: sparkEmitter
            system: particleSystem
            group: "sparks"
            enabled: false
            lifeSpan: 700
            lifeSpanVariation: 200
            size: 14
            sizeVariation: 6
            endSize: 4                     // 粒子结束时缩小
            velocity: AngleDirection {
                angle: 0
                angleVariation: 360
                magnitude: 300
                magnitudeVariation: 120
            }
            acceleration: AngleDirection { angle: 90; magnitude: 40 }   // 向下重力
        }

        Emitter {
            id: smokeEmitter
            system: particleSystem
            group: "smoke"
            enabled: false
            lifeSpan: 2200
            lifeSpanVariation: 500
            size: 40
            sizeVariation: 15
            endSize: 80                     // 烟雾扩散变大
            velocity: AngleDirection {
                angle: 270                   // 向上
                angleVariation: 30
                magnitude: 80
                magnitudeVariation: 40
            }
            acceleration: AngleDirection { angle: 270; magnitude: 15 }   // 持续向上浮力
        }

        Emitter {
            id: shockEmitter
            system: particleSystem
            group: "shockwave"
            enabled: false
            lifeSpan: 350
            lifeSpanVariation: 50
            size: 40
            sizeVariation: 10
            endSize: 140                    // 快速扩大
            velocity: AngleDirection {
                angle: 0
                angleVariation: 360
                magnitude: 400
                magnitudeVariation: 150
            }
        }

        Emitter {
            id: flashEmitter
            system: particleSystem
            group: "flash"
            enabled: false
            lifeSpan: 150
            lifeSpanVariation: 30
            size: 50
            sizeVariation: 20
            endSize: 80
            velocity: AngleDirection {
                angle: 0
                angleVariation: 360
                magnitude: 600
                magnitudeVariation: 200
            }
        }

        // ---------- 影响器：增加物理真实感 ----------
        // 对火花施加向下的重力
        Gravity {
            system: particleSystem
            groups: ["sparks"]
            magnitude: 50
            angle: 90                       // 角度：90度 = 向下
        }

        // 对烟雾施加向上的浮力（模拟热空气上升）
        Gravity {
            system: particleSystem
            groups: ["smoke"]
            magnitude: 25
            angle: 270                       // 270度 = 向上
        }

        // 摩擦力：让所有粒子逐渐减速
        Friction {
            system: particleSystem
            groups: ["sparks", "smoke", "shockwave"]
            factor: 0.03                     // 摩擦力系数
            threshold: 30                    // 速度高于此值时施加摩擦
        }

        // 湍流：增加随机扰动，增强爆炸的不规则感
        Turbulence {
            system: particleSystem
            groups: ["sparks", "smoke"]
            strength: 50                     // 扰动强度
            width: root.width
            height: root.height
        }

        // ---------- 交互与触发 ----------
        MouseArea {
            anchors.fill: parent
            onClicked: (mouse) => explode(mouse.x, mouse.y)
        }

        Timer {
            interval: 1800
            repeat: true
            running: true
            onTriggered: explode(Math.random() * root.width, Math.random() * root.height)
        }

        // 爆炸函数：定位发射器并爆发粒子
        function explode(x, y) {
            sparkEmitter.x = x; sparkEmitter.y = y
            smokeEmitter.x = x; smokeEmitter.y = y
            shockEmitter.x = x; shockEmitter.y = y
            flashEmitter.x = x; flashEmitter.y = y

            sparkEmitter.burst(400)           // 爆发指定数量粒子
            smokeEmitter.burst(150)
            shockEmitter.burst(30)
            flashEmitter.burst(120)

            // 轻微镜头震动
            root.scale = 1.02
            root.opacity = 0.98
            resetAnim.restart()
        }

        SequentialAnimation {
            id: resetAnim
            NumberAnimation { target: root; property: "scale"; to: 1.0; duration: 150; easing.type: Easing.OutQuad }
            NumberAnimation { target: root; property: "opacity"; to: 1.0; duration: 100 }
        }

        // 初始演示爆炸
        Component.onCompleted: explode(width/2, height/2)

        Text {
            anchors.bottom: parent.bottom
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#CCFFFFFF"
            font.pixelSize: 16
            text: "点击任意位置引爆炸弹 • 自动循环爆炸"
            style: Text.Outline
            styleColor: "#88000000"
        }
    }
}
