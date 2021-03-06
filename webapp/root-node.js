// Interface to root node

/*jslint node: true, getset: true, maxlen: 80 */

"use strict";

const SerialPort = require("serialport");
const parsers = SerialPort.parsers;
const parser = new parsers.Readline({
    delimiter: "\r\n"
});
var webSocket = require("./web-socket");
var cli = require("./cli");
var port;

function log(message) {
    if (message.type === "error" || message.type === "warn") {
        cli.logError(message.text);
    } else {
        cli.log(message.text);
    }
}

function connect(settings) {
    port = new SerialPort(settings.comName, {
        baudRate: 115200
    });
    port.pipe(parser);
    port.on("open", function () {
        var message = {type: "info", text: "Serial port opened"};
        log(message);
        webSocket.send(message);
        settings.onConnected();
    });
    port.on("close", function () {
        var message = {type: "warn", text: "Serial port closed"};
        log(message);
        webSocket.send(message);
    });
    port.on("error", function () {
        var message = {type: "error", text: "Serial port error"};
        log(message);
        webSocket.send(message);
        process.exit(1);
    });
    port.on("disconnected", function () {
        var message = {type: "warn", text: "Serial port disconnected"};
        log(message);
        webSocket.send(message);
    });
    parser.on("data", function (data) {
        var message = {type: "data", text: data};
        log(message);
        webSocket.send(message);
    });
}

function sendJson(modeChainJson) {
    port.write(modeChainJson);
}

module.exports = {
    connect: connect,
    sendJson: sendJson
};
