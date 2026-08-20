const SOURCE={output:1,input:2,camera:3,window:4};
const CODEC={auto:1,pcm:2,aac:3,h264:4,h265:5,wmv:6};
const STATE={recording:1,finalizing:2,completed:3,interrupted:4,failed:5};

export class RecordingManager{
  constructor({call,notify}){
    this.call=call;this.notify=notify;this.capabilities=null;this.timer=0;
    this.startDialog=document.createElement('dialog');this.startDialog.className='recording-start';
    this.startDialog.innerHTML='<form method="dialog"><h2 data-role="title">开始录制</h2><div data-role="options"></div><div class="dialog-actions"><button value="start">开始录制</button><button value="cancel" formnovalidate>取消</button></div></form>';
    this.listDialog=document.createElement('dialog');this.listDialog.className='recording-list';
    this.listDialog.innerHTML='<form method="dialog"><h2>录制任务</h2><div class="manager-table"><table><thead><tr><th>来源</th><th>格式</th><th>状态</th><th>开始时间</th><th>时长</th><th>大小</th><th></th></tr></thead><tbody></tbody></table><div class="manager-empty">没有录制任务</div></div><div class="dialog-actions"><button type="button" data-action="refresh">刷新</button><span class="spacer"></span><button value="close">关闭</button></div></form>';
    document.body.append(this.startDialog,this.listDialog);
    this.startDialog.addEventListener('close',()=>{if(this.startDialog.returnValue==='start')this.start()});
    this.listDialog.addEventListener('close',()=>this.stopPolling());
    this.listDialog.querySelector('[data-action=refresh]').onclick=()=>this.load();
    this.listDialog.querySelector('tbody').addEventListener('click',event=>this.listAction(event));
  }

  async audio(flow,deviceId){
    await this.open({source:flow===1?SOURCE.output:SOURCE.input,sourceId:deviceId||'',title:flow===1?'录制输出音频':'录制输入音频'});
  }

  async camera(deviceId){
    await this.open({source:SOURCE.camera,sourceId:deviceId,title:'录制摄像头'});
  }

  async window(windowHandle){
    await this.open({source:SOURCE.window,windowHandle,title:'录制窗口'});
  }

  async open(options){
    try{
      if(this.capabilities===null)this.capabilities=(await this.call('/api/recording/capabilities',{})).codecs;
      this.pending=options;this.startDialog.querySelector('[data-role=title]').textContent=options.title;
      const video=options.source===SOURCE.camera||options.source===SOURCE.window;
      const codecs=options.source<=SOURCE.input?[[CODEC.auto,'自动'],[CODEC.aac,'AAC'],[CODEC.pcm,'PCM']]:options.source===SOURCE.camera?[[CODEC.auto,'自动'],[CODEC.h265,'H.265 / HEVC'],[CODEC.h264,'H.264']]:[[CODEC.auto,'自动'],[CODEC.h265,'H.265 / HEVC'],[CODEC.h264,'H.264'],[CODEC.wmv,'Windows Media Video 9 Screen']];
      const available=codecs.filter(([value])=>value===CODEC.auto||this.capabilities&(1<<(value-1)));
      if(available.length===1&&available[0][0]===CODEC.auto)throw new Error('当前系统没有可用的录制编码器');
      let audioDevices=[];
      if(video)audioDevices=await this.call('/api/audio/devices',{});
      this.startDialog.querySelector('[data-role=options]').innerHTML=`<label>编码<select data-field="codec">${available.map(([value,name])=>`<option value="${value}">${name}</option>`).join('')}</select></label>${video?`<label>帧率<input data-field="frameRate" type="number" min="1" max="120" value="${options.source===SOURCE.window?12:30}" required></label><label>尺寸上限<select data-field="maxDimension"><option>640</option><option>1280</option><option selected>1920</option><option>3840</option><option value="7680">原始</option></select></label><label>视频码率（bit/s）<input data-field="videoBitRate" type="number" min="100000" max="100000000" value="4000000" required></label><label>音频<select data-field="audioSource"><option value="0">无</option><option value="1">系统输出</option><option value="2">麦克风</option></select></label><label data-role="audio-device" hidden>音频设备<select data-field="audioDeviceId"></select></label>${options.source===SOURCE.window?'<label><input data-field="cursor" type="checkbox">捕获鼠标指针</label>':''}`:`<label>音频码率（bit/s）<input data-field="audioBitRate" type="number" min="16000" max="1000000" value="160000" required></label>`}`;
      if(video){
        const source=this.startDialog.querySelector('[data-field=audioSource]'),device=this.startDialog.querySelector('[data-field=audioDeviceId]'),row=this.startDialog.querySelector('[data-role=audio-device]');
        const update=()=>{const flow=Number(source.value);row.hidden=!flow;device.replaceChildren(new Option('系统默认设备',''),...audioDevices.filter(item=>item.flow===flow&&(item.state&1)).map(item=>new Option(item.name||item.id,item.id)))};
        source.onchange=update;update();
      }
      this.startDialog.returnValue='';this.startDialog.showModal();
    }catch(error){this.notify(error)}
  }

  async start(){
    const field=name=>this.startDialog.querySelector(`[data-field=${name}]`),video=this.pending.source>=SOURCE.camera;
    const audioSource=video?Number(field('audioSource').value):0;
    const request={source:this.pending.source,codec:Number(field('codec').value),frameRate:video?Number(field('frameRate').value):0,audioSource,flags:video&&field('cursor')?.checked?1:0,maxDimension:video?Number(field('maxDimension').value):0,videoBitRate:video?Number(field('videoBitRate').value):0,audioBitRate:video&&audioSource||!video?160000:0,windowHandle:String(this.pending.windowHandle||0),sourceId:this.pending.sourceId||null,audioDeviceId:video&&audioSource?field('audioDeviceId').value||null:null};
    if(!video)request.audioBitRate=Number(field('audioBitRate').value);
    try{await this.call('/api/recording/start',request);await this.show()}catch(error){this.notify(error)}
  }

  async show(){this.listDialog.returnValue='';if(!this.listDialog.open)this.listDialog.showModal();await this.load()}

  async load(){
    try{
      const records=await this.call('/api/recording/list',{}),body=this.listDialog.querySelector('tbody');
      body.replaceChildren(...records.map(record=>this.row(record)));
      this.listDialog.querySelector('.manager-empty').hidden=records.length!==0;
      this.stopPolling();if(records.some(record=>record.state<=STATE.finalizing))this.timer=setTimeout(()=>this.load(),1000);
    }catch(error){this.stopPolling();this.notify(error)}
  }

  row(record){
    const row=document.createElement('tr');
    for(const value of [sourceName(record.source),codecName(record.codec),stateName(record),formatTime(record.startTime),formatDuration(record.duration),formatSize(record.fileSize)]){const cell=document.createElement('td');cell.textContent=value;row.append(cell)}
    const actions=document.createElement('td');
    if(record.state===STATE.recording){const stop=document.createElement('button');stop.dataset.action='stop';stop.dataset.id=record.recordingId;stop.textContent='停止';actions.append(stop)}
    if((record.state===STATE.completed||record.state===STATE.interrupted)&&BigInt(record.fileSize)&&record.path){const download=document.createElement('a');download.href=`/api/file/download?path=${encodeURIComponent(record.path)}`;download.download=record.path.split(/[\\/]/).pop();download.textContent='下载';actions.append(download)}
    if(record.state>=STATE.completed){const remove=document.createElement('button');remove.dataset.action='delete';remove.dataset.id=record.recordingId;remove.textContent='删除';actions.append(remove)}
    row.append(actions);return row;
  }

  async listAction(event){const button=event.target.closest('button[data-action]');if(!button)return;try{await this.call(`/api/recording/${button.dataset.action}`,{recordingId:Number(button.dataset.id)});await this.load()}catch(error){this.notify(error)}}
  stopPolling(){clearTimeout(this.timer);this.timer=0}
}

function sourceName(value){return ({1:'输出音频',2:'输入音频',3:'摄像头',4:'窗口'})[value]||`来源 ${value}`}
function codecName(value){return ({2:'PCM',3:'AAC',4:'H.264',5:'H.265',6:'WMV Screen'})[value]||`编码 ${value}`}
function stateName(record){if(record.state===STATE.failed)return `失败 · ${statusName(record.status)}`;return ({1:'正在录制',2:'正在完成',3:'已完成',4:'连接中断'})[record.state]||`状态 ${record.state}`}
function statusName(status){const type=['Success','NTSTATUS','Win32','Winsock','HRESULT','Security','QUIC','ProcessExit','ConfigurationManager'][status.type]||`Type ${status.type}`;return `${type}：0x${Number(status.code).toString(16).padStart(8,'0').toUpperCase()}`}
function formatTime(value){const ticks=BigInt(value);if(ticks<116444736000000000n)return '—';return new Date(Number((ticks-116444736000000000n)/10000n)).toLocaleString()}
function formatDuration(value){const seconds=Number(BigInt(value)/10000000n);return `${Math.floor(seconds/3600).toString().padStart(2,'0')}:${Math.floor(seconds/60%60).toString().padStart(2,'0')}:${(seconds%60).toString().padStart(2,'0')}`}
function formatSize(value){let size=Number(BigInt(value));for(const unit of ['B','KB','MB','GB']){if(size<1024||unit==='GB')return `${size.toFixed(unit==='B'?0:1)} ${unit}`;size/=1024}}
