
/**
 * Displays a help popup window
 */
tvheadend.help = function(title, pagename) {
    Ext.Ajax.request({
	url: 'docs/' + pagename,
	success: function(result, request) { 

	    var content = new Ext.Panel({
		autoScroll:true,
		border: false,
		layout:'fit', 
		html: result.responseText
	    });

	    var win = new Ext.Window({
		title: 'Help for ' + title,
		layout: 'fit',
		width: 900,
		height: 400,
		constrainHeader: true,
		items: [content]
	    });    
	    win.show();

	}});
}

/**
 * Displays a mediaplayer for HTML5 video tag
 */
tvheadend.MediaPlayer = function(url) {
    var video = document.createElement('video');
    video.setAttribute('autoplay', 'autoplay');
    video.setAttribute('preload', 'none');
    if(url) {
	video.setAttribute('src', url);
    }

    var selectChannel = new Ext.form.ComboBox({
	loadingText: 'Loading...',
	width: 200,
	displayField:'name',
	store: tvheadend.channels,
	mode: 'local',
	editable: false,
	triggerAction: 'all',
	emptyText: 'Select channel...'
    });
  
    selectChannel.on('select', function(c, r) {
	video.src = 'stream/channelid/' + r.data.chid + '?t=1';
    });
    
    var slider = new Ext.Slider({
	width: 135,
	height: 20,
	value: 90,
	increment: 1,
	minValue: 0,
	maxValue: 99
    });
  
    var sliderLabel = new Ext.form.Label();
    sliderLabel.setText("90%");

    slider.addListener('change', function() {
	video.volume = slider.getValue()/100.0;
	sliderLabel.setText(slider.getValue() + '%');
    });
  
    var win = new Ext.Window({
	title: 'Media Player',
	layout:'fit',
	width: 507 + 14,
	height: 384 + 56,
	constrainHeader: true,
	iconCls: 'eye',
	resizable: true,
	items: [video],
	tbar: [
	    selectChannel,
	    '-',
	    {
		iconCls: 'control_play',
		tooltip: 'Play',
		handler: function() {
		    video.play();
		}
	    },
	    {
		iconCls: 'control_pause',
		tooltip: 'Pause',
		handler: function() {
		    video.pause();
		}
	    },
	    {
		iconCls: 'control_stop',
		tooltip: 'Stop',
		handler: function() {
		    video.src = 'docresources/tvheadendlogo.png';
		}
            },
	    '-',
	    {
		iconCls: 'control_fullscreen',
		tooltip: 'Fullscreen',
		handler: function() {
		    if(video.requestFullScreen){
			video.requestFullScreen();
		    } else if(video.mozRequestFullScreen){
			video.mozRequestFullScreen();
		    } else if(video.webkitRequestFullScreen){
			video.webkitRequestFullScreen();
		    }
		}
	    },
	    '-',
	    {
		iconCls: 'control_volume',
		tooltip: 'Volume',
		disabled: true
	    },
	]
    });    

    win.on('beforeShow', function() {
	win.getTopToolbar().add(slider);
	win.getTopToolbar().add(new Ext.Toolbar.Spacer());
	win.getTopToolbar().add(new Ext.Toolbar.Spacer());
	win.getTopToolbar().add(new Ext.Toolbar.Spacer());
	win.getTopToolbar().add(sliderLabel);
	video.width = win.getInnerWidth();
	video.height = win.getInnerHeight();
    });

    win.on('bodyresize', function() {
	video.width = win.getInnerWidth();
	video.height = win.getInnerHeight();
    });

    win.on('close', function() {
	video.pause();
	video.src = 'docresources/tvheadendlogo.png';
    });
    win.show();
};

/**
 * This function creates top level tabs based on access so users without 
 * access to subsystems won't see them.
 *
 * Obviosuly, access is verified in the server too.
 */
function accessUpdate(o) {

    if(o.dvr == true && tvheadend.dvrpanel == null) {
	tvheadend.dvrpanel = new tvheadend.dvr;
	tvheadend.rootTabPanel.add(tvheadend.dvrpanel);
    }

    if(o.admin == true && tvheadend.confpanel == null) {
	tvheadend.confpanel = new Ext.TabPanel({
	    activeTab:0, 
	    autoScroll:true, 
	    title: 'Configuration', 
	    iconCls: 'wrench',
	    items: [new tvheadend.chconf,
		    new tvheadend.xmltv,
		    new tvheadend.cteditor,
		    new tvheadend.dvrsettings,
		    new tvheadend.tvadapters,
		    new tvheadend.iptv,
		    new tvheadend.acleditor, 
		    new tvheadend.cwceditor,
                    new tvheadend.capmteditor]
	});
	tvheadend.rootTabPanel.add(tvheadend.confpanel);
    }

    if(tvheadend.aboutPanel == null) {
	tvheadend.aboutPanel = new Ext.Panel({
	    border: false,
	    layout:'fit', 
	    title:'About',
	    iconCls:'info',
	    autoLoad: 'about.html'
	});
	tvheadend.rootTabPanel.add(tvheadend.aboutPanel);
    }

    tvheadend.rootTabPanel.doLayout();
}


/**
*
 */
function setServerIpPort(o) {
    tvheadend.serverIp = o.ip;
    tvheadend.serverPort = o.port;
}

function makeRTSPprefix() {
    return 'rtsp://' + tvheadend.serverIp + ':' + tvheadend.serverPort + '/';
}

/**
*
*/
tvheadend.log = function(msg, style) {
    s = style ? '<div style="' + style + '">' : '<div>'

    sl = Ext.get('systemlog');
    e = Ext.DomHelper.append(sl, s + '<pre>' + msg + '</pre></div>');
    e.scrollIntoView('systemlog');
}



/**
 *
 */
// create application
tvheadend.app = function() {
 
    // public space
    return {
 
        // public methods
        init: function() {

	    tvheadend.rootTabPanel = new Ext.TabPanel({
		region:'center',
		activeTab:0,
		items:[new tvheadend.epg]
	    });

	    var viewport = new Ext.Viewport({
		layout:'border',
		items:[
		    {
			region:'south',
			contentEl: 'systemlog',
			split:true,
			autoScroll:true,
			height: 150,
			minSize: 100,
			maxSize: 400,
			collapsible: true,
			title:'System log',
			margins:'0 0 0 0',
			tools:[{
			    id:'gear',
			    qtip: 'Enable debug output',
			    handler: function(event, toolEl, panel){
				Ext.Ajax.request({
				    url: 'comet/debug',
				    params : { 
					boxid: tvheadend.boxid
				    }
				});
			    }
			}]
		    },tvheadend.rootTabPanel
		]
	    });

	    tvheadend.comet.on('accessUpdate', accessUpdate);

	    tvheadend.comet.on('setServerIpPort', setServerIpPort);

	    tvheadend.comet.on('logmessage', function(m) {
		tvheadend.log(m.logtxt);
	    });

	    new tvheadend.cometPoller;

	    Ext.QuickTips.init();
	}
	
    };
}(); // end of app
 
