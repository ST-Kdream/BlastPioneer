import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Particles 2.15

Window
{
    width: 800
    height: 600
    visible: true
    title: "BlastPioneer"
    color: "black"

    // 由 C++ 传入的质量等级：0=低，1=中，2=高（默认）
    property int quality: 2

    Item
    {
        id: root
        anchors.fill: parent

        ParticleSystem
        {
            id: particleSystem
        }

        // ---------- 粒子外观定义 ----------
        ImageParticle
        {
            groups: ["sparks"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#FFAA33"
            colorVariation: 0.8
            alpha: 1.0
            alphaVariation: 0.3
            entryEffect: ImageParticle.Fade
            rotation: 0
            rotationVariation: 360
            rotationVelocity: 180
            rotationVelocityVariation: 90
        }

        ImageParticle
        {
            groups: ["smoke"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#AAAAAA"
            colorVariation: 0.4
            alpha: 0.7
            alphaVariation: 0.2
            entryEffect: ImageParticle.Fade
        }

        ImageParticle
        {
            groups: ["shockwave"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#88CCFF"
            colorVariation: 0.2
            alpha: 0.9
            alphaVariation: 0.1
            entryEffect: ImageParticle.Scale
        }

        ImageParticle
        {
            groups: ["flash"]
            source: "qrc:///particleresources/glowdot.png"
            color: "#FFFFFF"
            alpha: 1.0
            entryEffect: ImageParticle.Scale
        }

        // ---------- 粒子发射器 ----------
        Emitter
        {
            id: sparkEmitter
            system: particleSystem
            group: "sparks"
            enabled: false
            lifeSpan: 800
            lifeSpanVariation: 200
            size: 14
            sizeVariation: 6
            endSize: 4
            velocity: AngleDirection
            {
                angle: 0
                angleVariation: 360
                magnitude: 300
                magnitudeVariation: 120
            }
            acceleration: AngleDirection { angle: 90; magnitude: 40 }
        }

        Emitter
        {
            id: smokeEmitter
            system: particleSystem
            group: "smoke"
            enabled: false
            lifeSpan: 5000
            lifeSpanVariation: 1000
            size: 40
            sizeVariation: 15
            endSize: 120
            velocity: AngleDirection
            {
                angle: 270
                angleVariation: 30
                magnitude: 60
                magnitudeVariation: 30
            }
            acceleration: AngleDirection { angle: 270; magnitude: 10 }
        }

        Emitter
        {
            id: shockEmitter
            system: particleSystem
            group: "shockwave"
            enabled: false
            lifeSpan: 400
            lifeSpanVariation: 50
            size: 40
            sizeVariation: 10
            endSize: 150
            velocity: AngleDirection
            {
                angle: 0
                angleVariation: 360
                magnitude: 400
                magnitudeVariation: 150
            }
        }

        Emitter
        {
            id: flashEmitter
            system: particleSystem
            group: "flash"
            enabled: false
            lifeSpan: 180
            lifeSpanVariation: 30
            size: 50
            sizeVariation: 20
            endSize: 80
            velocity: AngleDirection
            {
                angle: 0
                angleVariation: 360
                magnitude: 600
                magnitudeVariation: 200
            }
        }

        // ---------- 影响器 ----------
        Gravity
        {
            system: particleSystem
            groups: ["sparks"]
            magnitude: 50
            angle: 90
        }

        Gravity
        {
            system: particleSystem
            groups: ["smoke"]
            magnitude: 25
            angle: 270
        }

        Friction
        {
            system: particleSystem
            groups: ["sparks", "smoke", "shockwave"]
            factor: 0.03
            threshold: 30
        }

        Turbulence
        {
            system: particleSystem
            groups: ["sparks", "smoke"]
            strength: 50
            width: root.width
            height: root.height
        }

        // ---------- 交互与自动循环 ----------
        MouseArea
        {
            anchors.fill: parent
            onClicked: (mouse) => explode(mouse.x, mouse.y)
        }

        // 自动循环爆炸，间隔5秒（与烟雾生命周期匹配）
        Timer
        {
            interval: 5000
            repeat: true
            running: true
            onTriggered: explode(Math.random() * root.width, Math.random() * root.height)
        }

        // 爆炸函数：根据质量等级调整粒子数量
        function explode(x, y)
        {
            sparkEmitter.x = x; sparkEmitter.y = y
            smokeEmitter.x = x; smokeEmitter.y = y
            shockEmitter.x = x; shockEmitter.y = y
            flashEmitter.x = x; flashEmitter.y = y

            // 默认高质量粒子数
            var sparkCount = 400
            var smokeCount = 150
            var shockCount = 30
            var flashCount = 120

            if (quality === 0)
            { // 低
                sparkCount = 150
                smokeCount = 60
                shockCount = 10
                flashCount = 40
            } else if (quality === 1)
            { // 中
                sparkCount = 250
                smokeCount = 100
                shockCount = 20
                flashCount = 80
            } // quality === 2 使用默认值

            sparkEmitter.burst(sparkCount)
            smokeEmitter.burst(smokeCount)
            shockEmitter.burst(shockCount)
            flashEmitter.burst(flashCount)

            // 轻微镜头震动
            root.scale = 1.02
            root.opacity = 0.98
            resetAnim.restart()
        }

        SequentialAnimation
        {
            id: resetAnim
            NumberAnimation { target: root; property: "scale"; to: 1.0; duration: 150; easing.type: Easing.OutQuad }
            NumberAnimation { target: root; property: "opacity"; to: 1.0; duration: 100 }
        }

        // 初始演示爆炸
        Component.onCompleted: explode(width/2, height/2)

        // ---------- 标题与加载指示 ----------
        Text
        {
            id: titleText
            anchors.top: parent.top
            anchors.topMargin: 20
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#FFD700"
            font.pixelSize: 32
            font.bold: true
            text: "BlastPioneer"
            style: Text.Outline
            styleColor: "#80000000"
        }

        Text
        {
            anchors.top: titleText.bottom
            anchors.topMargin: 8
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#88FFFFFF"
            font.pixelSize: 18
            text: "Loading..."
            style: Text.Outline
            styleColor: "#88000000"
        }
    }
}
