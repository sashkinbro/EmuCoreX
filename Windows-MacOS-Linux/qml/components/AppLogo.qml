import QtQuick

Item {
    implicitWidth: 40
    implicitHeight: 40

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        antialiasing: true

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const context = getContext("2d")
            context.reset()
            const scale = Math.min(width, height) / 108
            context.translate((width - 108 * scale) / 2, (height - 108 * scale) / 2)
            context.scale(scale, scale)

            context.fillStyle = "#0D0D0F"
            context.beginPath()
            context.roundedRect(0, 0, 108, 108, 24, 24)
            context.fill()

            function polygon(points, color) {
                context.fillStyle = color
                context.beginPath()
                context.moveTo(points[0], points[1])
                for (let index = 2; index < points.length; index += 2)
                    context.lineTo(points[index], points[index + 1])
                context.closePath()
                context.fill()
            }
            polygon([30,28, 50,48, 42,48, 22,28], "#4D9EFF")
            polygon([78,28, 58,48, 66,48, 86,28], "#4D9EFF")
            polygon([30,80, 50,60, 42,60, 22,80], "#3A7FD5")
            polygon([78,80, 58,60, 66,60, 86,80], "#3A7FD5")
            polygon([49,50, 54,44, 59,50, 54,56], "#70B4FF")
        }
    }
}
