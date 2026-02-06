import QtQuick
import QtQuick.Controls
import QtQuick.Particles

Item
{
   Rectangle
   {
       id: root
       width: 800
       height: 600
       color: "#1A1A1A"
   }

   ParticleSystem
   {
       id: particleSystem
   }

   //============粒子=============
   //核心粒子
   ImageParticle
   {
       id: coreParticle
       system: particleSystem
       source: ""
       width: 8
       height: 8
       color: "#FFCC00"
       colorVariation: 0.3
   }
   //火花粒子
   ImageParticle
   {
       id: sparkParticle
       system: particleSystem
       source: ""
       width: 4
       height: 4
       color: "#E62E00"
       colorVariation: 0.3
       opacity: 0.8
   }

   //==========发射器============
   //核心粒子发射器
   Emitter
   {
       id: coreEmitter
       system: particleSystem
       anchors.centerIn: parent
       width: 10
       height: 10

   }
}
