#!/usr/bin/env node
/*jshint node: true */
'use strict';

var NATS = require('nats')
const natsUri = "nats://192.168.2.105:4222" // NANOPI NEO
var nats = NATS.connect({ url: natsUri })

// Simple Subscriber
nats.subscribe('axon.>', function(msg, reply, subject) {
    const replyTo = reply ? reply : '___'
    console.log(`${msg}`)
})
