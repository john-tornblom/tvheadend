
Ext.namespace('htsp');

htsp.Socket = Ext.extend(Ext.util.Observable, {
    constructor: function(config) {
	config = config || {};

	this.addEvents({
	    hello: true,
	    close: true,
	    error: true,
	    channelAdd: true,
	    channelUpdate: true,
	    channelDelete: true,
	    tagAdd: true,
	    tagUpdate: true,
	    tagDelete: true,
	    dvrEntryAdd: true,
	    dvrEntryUpdate: true,
	    dvrEntryDelete: true,
	    eventAdd: true,
	    eventUpdate: true,
	    eventDelete: true,
	    initialSyncCompleted: true
	});

	this.listeners = config.listeners;

	var host = document.location.hostname;
	var port = document.location.port;
	var path = document.location.pathname;
	var dir = path.substring(path.indexOf('/', 1) + 1, path.lastIndexOf('/'));
	this.url = 'ws://' + host + ':' + port + dir + '/htsp';
	this.callbacks = {};
	this.seq = 0;

	htsp.Socket.superclass.constructor.call(this, config);
    },

    open: function(url) {
	url = url || this.url;
	var ws = new WebSocket(url, 'htsp');

	var helloMessage = {
		method: 'hello',
		clientname: 'Tvheadend Websocket',
		clientversion: '0',
		htspversion: 11
	};
	var onHelloResponse = function(msg) {
	    this.fireEvent('hello', msg);
	}.bind(this);

	var onData = function(m) {
	    var msg;
	    if(typeof m.data == 'string')
		msg = JSON.parse(m.data);

	    if(!msg)
		return;

	    if(msg.seq in this.callbacks) {
		var cb = this.callbacks[msg.seq];
		delete this.callbacks[msg.seq];
		cb(msg);
	    } else if(msg.method) {
		this.fireEvent(msg.method, msg);
	    } else {
		console.log('unhandled message');
		console.dir(msg);
	    }
	}.bind(this);

	ws.addEventListener('open', function() {
	    this.send(helloMessage, onHelloResponse);
	}.bind(this));

	ws.addEventListener('message', function(m) {
	    onData(m);
	}.bind(this));

	ws.addEventListener('error', function(e) {
	    this.fireEvent('error', e);
	}.bind(this));

	ws.addEventListener('close', function() {
	    this.fireEvent('close');
	}.bind(this));

	this.ws = ws;
    },

    send: function(msg, cb) {
	cb = cb || function(){};

	msg.seq = this.seq;
	msg = JSON.stringify(msg);

	this.callbacks[this.seq] = cb;
	this.seq += 1;
	this.ws.send(msg);
    },

    close: function() {
	this.ws.close();
    },

    enableAsync: function(config) {
	config = config || {};
	config.method = 'enableAsyncMetadata';
	this.send(config);
    }
});


htsp.Client = Ext.extend(Ext.util.Observable, {
    constructor: function(config) {
	config = config || {};
	this.sock = config.sock || new htsp.Socket();
	this.listeners = config.listeners;
	htsp.Client.superclass.constructor.call(this, config);
	
	this.channelStore = new Ext.data.JsonStore({
	    idProperty: 'channelId',
	    fields: [
		{name: 'id',     mapping: 'channelId'},
		{name: 'number', mapping: 'channelNumber', defaultValue: null},
		{name: 'name',   mapping: 'channelName',   defaultValue: null},
		{name: 'icon',   mapping: 'channelIcon',   defaultValue: null},
		{name: 'now',    mapping: 'eventId',       defaultValue: null},
		{name: 'next',   mapping: 'nextEventId',   defaultValue: null},
		{name: 'tags',                             defaultValue: null},
		{name: 'services',                         defaultValue: null}
	    ]
	});

	this.tagStore = new Ext.data.JsonStore({
	    idProperty: 'tagId',
	    fields: [
		{name: 'id',     mapping: 'tagId'},
		{name: 'name',   mapping: 'tagName', defaultValue: null},
		{name: 'icon',   mapping: 'tagIcon', defaultValue: null},
		{name: 'members',                    defaultValue: null}
	    ]
	});

	this.dvrStore = new Ext.data.JsonStore({
	    idProperty: 'id',
	    fields: [
		{name: 'id'},
		{name: 'channelId', mapping: 'channel', defaultValue: null},
		{name: 'eventId',                       defaultValue: null},
		{name: 'title',                         defaultValue: null},
		{name: 'summary',                       defaultValue: null},
		{name: 'description',                   defaultValue: null},
		{name: 'state',                         defaultValue: null},
		{name: 'error',                         defaultValue: null},
		{name: 'start', type: 'date', dateFormat: 'U'},
		{name: 'stop',  type: 'date', dateFormat: 'U'}
	    ]
	});

	this.epgStore = new Ext.data.JsonStore({
	    idProperty: 'eventId',
	    fields: [
		{name: 'id',          mapping: 'eventId'},
		{name: 'channelId'},
		{name: 'title',       defaultValue: null},
		{name: 'summary',     defaultValue: null},
		{name: 'description', defaultValue: null},
		{name: 'start',       type: 'date', dateFormat: 'U'},
		{name: 'stop',        type: 'date', dateFormat: 'U'}
	    ]
	});

	var syncData = {
	    channels: [],
	    tags: [],
	    epg: []
	};

	var updateRecord = function(store, msg) {
	    var r = store.getById(msg[store.idProperty]);
	    var o = store.reader.readRecords(msg);
	    if(!o || !o.records.length)
		return;
	    
	    var data = o.records[0].data;
	    for(var prop in data) {
		if(data[prop] === undefined || data[prop] === null)
		    continue;

		console.log(prop + ': ' + r.data[prop] + ' ---> ' + data[prop]);
		r.data[prop] = data[prop];
	    }
	    r.commit();
	};

	/********* Setup initial data sync ***********/
	this.sock.on('hello', function(msg) {
	    this.enableAsync({
		epg: 1,
		epgMaxTime: new Date().getTime() / 1000 // limit to current events
	    });
	});

	this.sock.on('initialSyncCompleted', function() {
	    this.channelStore.sort('number');
	    this.epgStore.sort('start');

	    this.channelStore.loadData(syncData.channels);
	    this.epgStore.loadData(syncData.epg);
	    
	    delete syndData;
	    syncData = undefined;
	}.bind(this));


	/********* Handle channel events ***********/
	this.sock.on('channelAdd', function(msg) {
	    if(!syncData)
		this.channelStore.loadData(msg, true);
	    else
		syncData.channels.push(msg);

	}.bind(this));

	this.sock.on('channelUpdate', function(msg) {
	    if(!syncData)
		updateRecord(this.channelStore, msg);
	    else
		syncData.channels.push(msg);
	}.bind(this));

	this.sock.on('channelDelete', function(msg) {
	    var index = this.channelStore.indexOfId(msg.channelId);
	    this.channelStore.removeAt(index);
	}.bind(this));


	/********* Handle epg events ***********/
	this.sock.on('eventAdd', function(msg) {
	    if(!syncData)
		this.epgStore.loadData(msg, true);
	    else
		syncData.epg.push(msg);
	}.bind(this));

	this.sock.on('eventUpdate', function(msg) {
	    if(!syncData)
		updateRecord(this.epgStore, msg);
	    else
		syncData.epg.push(msg);
	}.bind(this));

	this.sock.on('eventDelete', function(msg) {
	    var index = this.epgStore.indexOfId(msg.channelId);
	    this.epgStore.removeAt(index);
	}.bind(this));
    },

    connect: function(url) {
	this.sock.open(url);
    }
});

