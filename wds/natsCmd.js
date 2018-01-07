#!/usr/bin/env node
/*jshint node: true */
'use strict';

var NATS = require('nats');
const natsUri = "nats://192.168.2.105:4222"
var nats = NATS.connect({ url: natsUri });

const deviceId = "6cfde020-fcd8-493f-9f6c-d8415b4a3fd5"
const topicPrefix = 'axon'
const topicUpStream = topicPrefix + '.gateway.' + deviceId
const topicDownStream = topicPrefix + '.devices.' + deviceId

const sendCommand = command => {

    nats.requestOne(topicDownStream, command, {}, 10000, function(response) {

        console.log(JSON.stringify(response))
        if(response.code && response.code === NATS.REQ_TIMEOUT) {
            console.log('Request for help timed out.');
        }
        nats.close()
    })
}

sendCommand(process.argv[2])
