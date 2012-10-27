tvheadend.acleditor = function() {
	var fm = Ext.form;

	var enabledColumn = new Ext.grid.CheckColumn({
		header : "Enabled",
		dataIndex : 'enabled',
		width : 60
	});

	var streamingColumn = new Ext.grid.CheckColumn({
		header : "Streaming",
		dataIndex : 'streaming',
		width : 100
	});

	var dvrColumn = new Ext.grid.CheckColumn({
		header : "Video Recorder",
		dataIndex : 'dvr',
		width : 100
	});

	var dvrallcfgColumn = new Ext.grid.CheckColumn({
		header : "All Configs (VR)",
		dataIndex : 'dvrallcfg',
		width : 100
	});

	var webuiColumn = new Ext.grid.CheckColumn({
		header : "Web Interface",
		dataIndex : 'webui',
		width : 100
	});

	var adminColumn = new Ext.grid.CheckColumn({
		header : "Admin",
		dataIndex : 'admin',
		width : 100
	});

	var transcodeColumn = new Ext.grid.CheckColumn({
		header : "Transcode",
		dataIndex : 'transcode',
		width : 100
	});

	var cm = new Ext.grid.ColumnModel({
  defaultSortable: true,
  columns : [ enabledColumn, {
		header : "Username",
		dataIndex : 'username',
		editor : new fm.TextField({
			allowBlank : false
		})
	}, {
		header : "Password",
		dataIndex : 'password',
		renderer : function(value, metadata, record, row, col, store) {
			return '<span class="tvh-grid-unset">Hidden</span>';
		},
		editor : new fm.TextField({
			allowBlank : false
		})
	}, {
		header : "Prefix",
		dataIndex : 'prefix',
		editor : new fm.TextField({
			allowBlank : false
		})
	}, streamingColumn, dvrColumn, dvrallcfgColumn, webuiColumn, adminColumn, transcodeColumn, {
                header : "Resolution",
                dataIndex : 'resolution',
                width : 100,
                editor : new fm.ComboBox({
                  store: new Ext.data.SimpleStore({
                    fields : [ 'key', 'value' ],
                    data: [
                      ['720','720p'],
                      ['576','576p'],
                      ['480','480p'],
                      ['384','384p'],
                      ['288','288p']
                    ]
                  }),
                  displayField: 'value',
                  valueField: 'key',
                  typeAhead: true,
                  mode: 'local',
                  triggerAction: 'all',
                  selectOnFocus: true
                })
        }, {
                header : "VCodec",
                dataIndex : 'vcodec',
                width : 100,
                editor : new fm.ComboBox({
                  store: new Ext.data.SimpleStore({
                    fields : [ 'key','value' ],
                    data: [
                      ['h264','h264'],
                      ['vp8','vp8']
                    ]
                  }),
                  displayField: 'key',
                  valueField: 'value',
                  typeAhead: true,
                  mode: 'local',
                  triggerAction: 'all',
                  selectOnFocus: true
                })
        }, {
                header : "ACodec",
                dataIndex : 'acodec',
                width : 100,
                editor : new fm.ComboBox({
                  store: new Ext.data.SimpleStore({
                    fields : [ 'key','value' ],
                    data: [ 
                      ['aac','aac'],
                      ['vorbis','vorbis'],
                      ['ac3','ac3']
                    ]
                  }),
                  displayField: 'key',
                  valueField: 'value',
                  typeAhead: true,
                  mode: 'local',
                  triggerAction: 'all',
                  selectOnFocus: true
                })
        }, {
                header : "SCodec",
                dataIndex : 'scodec',
                width : 100,
                editor : new fm.TextField({})
        }, {
		header : "Comment",
		dataIndex : 'comment',
		width : 400,
		editor : new fm.TextField({})
	} ]});

	var UserRecord = Ext.data.Record.create([ 'enabled', 'streaming', 'dvr',
		'dvrallcfg', 'admin', 'webui', 'username', 'prefix', 'password',
		'comment', 'transcode', 'resolution', 'vcodec', 'acodec', 'scodec' ]);

	return new tvheadend.tableEditor('Access control', 'accesscontrol', cm,
		UserRecord, [ enabledColumn, streamingColumn, dvrColumn, dvrallcfgColumn,
			webuiColumn, adminColumn, transcodeColumn ], null, 'config_access.html', 'group');
}
