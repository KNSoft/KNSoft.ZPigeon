const ROOTS=[
  [1,'HKEY_CLASSES_ROOT'],[2,'HKEY_CURRENT_USER'],[3,'HKEY_LOCAL_MACHINE'],
  [4,'HKEY_USERS'],[5,'HKEY_CURRENT_CONFIG']
];
const TYPES=new Map([[0,'REG_NONE'],[1,'REG_SZ'],[2,'REG_EXPAND_SZ'],[3,'REG_BINARY'],[4,'REG_DWORD'],[5,'REG_DWORD_BIG_ENDIAN'],[6,'REG_LINK'],[7,'REG_MULTI_SZ'],[8,'REG_RESOURCE_LIST'],[9,'REG_FULL_RESOURCE_DESCRIPTOR'],[10,'REG_RESOURCE_REQUIREMENTS_LIST'],[11,'REG_QWORD']]);
const EDIT_TYPES=[[1,'字符串值'],[2,'可扩充字符串值'],[3,'二进制值'],[4,'DWORD (32 位) 值'],[7,'多字符串值'],[11,'QWORD (64 位) 值']];

export class RegistryEditor{
  constructor(host,{call,notify}){
    this.host=host;this.call=call;this.notify=notify;this.selected=null;this.values=[];this.valueRequest=0;
    host.innerHTML=`
      <div class="registry-toolbar">
        <div class="registry-address" data-role="address"></div>
      </div>
      <div class="registry-body"><aside class="registry-tree" tabindex="0"><ul data-role="tree"></ul></aside><div class="registry-list" tabindex="0"><table><thead><tr><th>名称</th><th>类型</th><th>数据</th></tr></thead><tbody data-role="values"></tbody></table><button class="registry-more" data-action="more" hidden>加载更多</button><div class="registry-empty" data-role="empty">请选择左侧注册表项</div></div></div>
      <div class="context-menu" data-role="key-menu" hidden><button data-action="key-new">新建项</button><button data-action="key-rename">重命名</button><button data-action="key-delete" class="danger">删除</button></div>
      <div class="context-menu" data-role="value-menu" hidden><button data-action="value-modify">修改</button><button data-action="value-new-1">新建字符串值</button><button data-action="value-new-2">新建可扩充字符串值</button><button data-action="value-new-3">新建二进制值</button><button data-action="value-new-4">新建 DWORD 值</button><button data-action="value-new-7">新建多字符串值</button><button data-action="value-new-11">新建 QWORD 值</button><button data-action="value-rename">重命名</button><button data-action="value-delete" class="danger">删除</button><button data-action="value-refresh">刷新</button></div>
      <dialog data-role="name-dialog"><form method="dialog"><h2 data-role="name-title"></h2><input data-role="name-input" class="dialog-input" maxlength="32767" required autocomplete="off"><div class="dialog-actions"><button value="cancel" formnovalidate>取消</button><button value="ok">确定</button></div></form></dialog>
      <dialog data-role="value-dialog"><form method="dialog"><h2 data-role="value-title"></h2><label>数值名称</label><input data-role="value-name" class="dialog-input" maxlength="32767" autocomplete="off"><label>数值类型</label><select data-role="value-type"></select><label data-role="base-label">基数</label><select data-role="value-base"><option value="16">十六进制</option><option value="10">十进制</option></select><label>数值数据</label><textarea data-role="value-data" class="registry-value-data" spellcheck="false"></textarea><p data-role="value-hint" class="status"></p><div class="dialog-actions"><button value="cancel">取消</button><button value="save">确定</button></div></form></dialog>`;
    this.tree=this.$('tree');this.body=this.$('values');this.address=this.$('address');this.more=this.action('more');this.empty=this.$('empty');
    this.keyMenu=this.$('key-menu');this.valueMenu=this.$('value-menu');this.nameDialog=this.$('name-dialog');this.valueDialog=this.$('value-dialog');
    this.bind();this.resetTree();
  }

  $(role){return this.host.querySelector(`[data-role="${role}"]`)}
  action(name){return this.host.querySelector(`[data-action="${name}"]`)}
  bind(){
    this.more.onclick=()=>this.loadValues(true);
    this.action('key-new').onclick=()=>{this.hideMenus();this.newKey()};this.action('key-rename').onclick=()=>{this.hideMenus();this.renameKey()};this.action('key-delete').onclick=()=>{this.hideMenus();this.deleteKey()};
    this.action('value-modify').onclick=()=>{const value=this.contextValue;this.hideMenus();if(value)this.editValue(value)};for(const [type] of EDIT_TYPES)this.action(`value-new-${type}`).onclick=()=>{this.hideMenus();this.newValue(type)};this.action('value-rename').onclick=()=>{const value=this.contextValue;this.hideMenus();if(value)this.renameValue(value)};this.action('value-delete').onclick=()=>{const value=this.contextValue;this.hideMenus();if(value)this.deleteValue(value)};this.action('value-refresh').onclick=()=>{this.hideMenus();this.loadValues()};
    this.nameDialog.addEventListener('close',()=>this.finishName());this.valueDialog.addEventListener('close',()=>this.finishValue());this.$('value-type').onchange=()=>this.updateValueEditor();
    this.host.addEventListener('pointerdown',event=>{if(!event.target.closest('.context-menu'))this.hideMenus()});
    this.host.addEventListener('keydown',event=>this.onKey(event));
    this.host.querySelector('.registry-list').oncontextmenu=event=>{if(event.target.closest('tbody tr'))return;event.preventDefault();this.contextValue=null;this.openValueMenu(event)};
  }

  resetTree(){
    this.tree.replaceChildren();this.selected=null;this.values=[];this.valueRequest++;this.body.replaceChildren();this.empty.hidden=false;this.address.textContent='';
    for(const [root,name] of ROOTS){const node={root,name,path:'',parent:null,hasChildren:true,loaded:false,loading:false,expanded:false};node.element=this.renderNode(node);this.tree.append(node.element)}
  }

  renderNode(node){
    const li=document.createElement('li'),row=document.createElement('div'),arrow=document.createElement('button'),label=document.createElement('button'),children=document.createElement('ul');
    li.className='registry-node';row.className='registry-node-row';arrow.className='registry-arrow';arrow.textContent=node.hasChildren?'▸':'';arrow.disabled=!node.hasChildren;arrow.tabIndex=-1;label.className='registry-node-label';label.textContent=node.name;children.hidden=true;
    row.append(arrow,label);li.append(row,children);node.row=row;node.arrow=arrow;node.label=label;node.children=children;
    arrow.onclick=event=>{event.stopPropagation();this.toggle(node)};label.onclick=event=>{if(event.detail>1)return;this.select(node);this.toggle(node)};row.oncontextmenu=event=>{event.preventDefault();this.select(node);this.openMenu(this.keyMenu,event);this.action('key-rename').disabled=this.action('key-delete').disabled=node.parent===null};
    return li;
  }

  async toggle(node){
    if(!node.hasChildren||node.loading)return;if(!node.loaded)await this.loadChildren(node);else{node.children.replaceChildren();node.loaded=node.expanded=false;node.children.hidden=true;node.cursor=null;node.arrow.textContent='▸'}
  }

  async loadChildren(node,append=false){
    if(node.loading)return;node.loading=true;
    try{
      const page=await this.call('/api/registry/keys',{root:node.root,path:node.path,cursor:append?node.cursor:null,maxEntries:256});
      if(!append)node.children.replaceChildren();else node.children.querySelector('.registry-more-node')?.remove();
      for(const record of page.records){const path=node.path?`${node.path}\\${record.name}`:record.name,child={root:node.root,name:record.name,path,parent:node,hasChildren:record.hasChildren,loaded:false,loading:false,expanded:false};child.element=this.renderNode(child);node.children.append(child.element)}
      if(page.hasMore){const more=document.createElement('button');more.className='registry-more-node';more.textContent='加载更多…';more.onclick=()=>this.loadChildren(node,true);node.children.append(more)}
      node.cursor=page.nextCursor;node.hasChildren=node.children.childElementCount!==0;node.loaded=true;node.expanded=node.hasChildren;node.children.hidden=!node.expanded;node.arrow.disabled=!node.hasChildren;node.arrow.textContent=node.hasChildren?'▾':'';
    }catch(error){this.notify(error)}finally{node.loading=false}
  }

  select(node){
    this.selected?.row.classList.remove('selected');this.selected=node;node.row.classList.add('selected');this.address.textContent=`${ROOTS.find(root=>root[0]===node.root)[1]}${node.path?`\\${node.path}`:''}`;this.values=[];this.body.replaceChildren();this.more.hidden=true;this.empty.textContent='正在读取…';this.empty.hidden=false;this.loadValues();
  }

  async loadValues(append=false){
    if(!this.selected)return;const selected=this.selected,request=++this.valueRequest,cursor=append?this.valueCursor:null;
    try{
      const page=await this.call('/api/registry/values',{root:selected.root,path:selected.path,cursor,maxEntries:128});
      if(request!==this.valueRequest||selected!==this.selected)return;
      const records=page.records.map(value=>({...value,data:base64Bytes(value.preview),exists:true}));
      if(!append){this.values=records;if(!records.some(value=>value.name===''))this.values.unshift({name:'',type:1,dataLength:0,data:new Uint8Array(),exists:false})}else this.values.push(...records);
      this.valueCursor=page.nextCursor;this.more.hidden=!page.hasMore;this.renderValues();
    }catch(error){if(request!==this.valueRequest||selected!==this.selected)return;if(!append){this.empty.textContent=error.message;this.empty.hidden=false}this.notify(error)}
  }

  renderValues(){
    this.body.replaceChildren();this.empty.hidden=true;
    for(const value of this.values){
      const row=document.createElement('tr');row.tabIndex=0;row.dataset.name=value.name;const name=document.createElement('td'),type=document.createElement('td'),data=document.createElement('td');
      name.textContent=value.name||'(默认)';type.textContent=typeName(value.type);data.textContent=value.exists?formatData(value.type,value.data,value.dataLength):'(数值未设置)';row.append(name,type,data);
      row.onclick=()=>this.selectValue(row,value);row.ondblclick=()=>this.editValue(value);row.oncontextmenu=event=>{event.preventDefault();event.stopPropagation();this.selectValue(row,value);this.contextValue=value;this.openValueMenu(event)};this.body.append(row);
    }
  }

  selectValue(row,value){this.body.querySelector('.selected')?.classList.remove('selected');row.classList.add('selected');this.selectedValue=value}
  async newKey(){
    if(!this.selected)return;const name=await this.askName('新建项','新建项 #1');if(!name)return;if(!validKeyName(name)){this.notify('项名称不能包含反斜杠');return}
    try{await this.call('/api/registry/key/create',this.scope({path:this.selected.path?`${this.selected.path}\\${name}`:name}));this.selected.loaded=false;await this.loadChildren(this.selected)}catch(error){this.notify(error)}
  }

  async renameKey(){
    const node=this.selected;if(!node?.parent)return;const name=await this.askName('重命名项',node.name);if(!name||name===node.name)return;if(!validKeyName(name)){this.notify('项名称不能包含反斜杠');return}
    try{await this.call('/api/registry/key/rename',this.scope({path:node.parent.path,name:node.name,newName:name}));const parent=node.parent;this.select(parent);parent.loaded=false;await this.loadChildren(parent)}catch(error){this.notify(error)}
  }

  async deleteKey(){
    const node=this.selected;if(!node?.parent||!confirm(`确定永久删除“${node.name}”及其所有子项和值吗？`))return;
    try{await this.call('/api/registry/key/delete',this.scope({path:node.path}));const parent=node.parent;this.select(parent);parent.loaded=false;await this.loadChildren(parent)}catch(error){this.notify(error)}
  }

  async newValue(type=1){
    if(!this.selected)return;const value={name:'新值 #1',type,data:new Uint8Array(),exists:false};this.openValueEditor('新建值',value,true);
  }
  async editValue(value){
    if(!this.selected)return;if(!value.exists){this.openValueEditor(`编辑 ${value.name||'(默认)'}`,value,false);return}
    try{const result=await this.call('/api/registry/value/query',this.scope({path:this.selected.path,name:value.name}));this.openValueEditor(`编辑 ${value.name||'(默认)'}`,{...value,type:result.type,data:base64Bytes(result.data)},false)}catch(error){this.notify(error)}
  }

  openValueEditor(title,value,creating){
    this.editing={value,creating};this.$('value-title').textContent=title;this.$('value-name').value=value.name;this.$('value-name').readOnly=!creating||value.name==='';
    const type=this.$('value-type');type.replaceChildren(...EDIT_TYPES.map(([id,name])=>new Option(`${name} (${typeName(id)})`,id)));if(!EDIT_TYPES.some(item=>item[0]===value.type))type.add(new Option(typeName(value.type),value.type));type.value=String(value.type);type.disabled=!creating;
    this.$('value-data').value=editorData(value.type,value.data,16);this.$('value-base').value='16';this.valueDialog.returnValue='';this.updateValueEditor();this.valueDialog.showModal();this.$('value-name').readOnly?this.$('value-data').focus():this.$('value-name').select();
  }

  updateValueEditor(){
    const type=Number(this.$('value-type').value),numeric=type===4||type===11;this.$('base-label').hidden=this.$('value-base').hidden=!numeric;
    this.$('value-hint').textContent=type===3||!EDIT_TYPES.some(item=>item[0]===type)?'请直接输入十六进制字节，可用空格或换行分隔。':type===7?'每行表示一个字符串。':numeric?'支持十六进制或十进制整数。':'';
  }

  async finishValue(){
    if(this.valueDialog.returnValue!=='save'||!this.editing)return;const {creating}=this.editing,name=this.$('value-name').value,type=Number(this.$('value-type').value),base=Number(this.$('value-base').value);this.editing=null;
    try{if(creating&&this.values.some(value=>value.exists&&value.name.toLocaleLowerCase()===name.toLocaleLowerCase()))throw new Error('同名数值已存在');const data=parseEditorData(type,this.$('value-data').value,base);await this.call('/api/registry/value/set',this.scope({path:this.selected.path,name,type,data:bytesBase64(data)}));await this.loadValues()}catch(error){this.notify(error)}
  }

  async renameValue(value){
    if(!value.exists||value.name==='')return;const name=await this.askName('重命名值',value.name);if(!name||name===value.name)return;
    try{await this.call('/api/registry/value/rename',this.scope({path:this.selected.path,name:value.name,newName:name}));await this.loadValues()}catch(error){this.notify(error)}
  }

  async deleteValue(value){
    if(!value.exists||value.name===''||!confirm(`确定永久删除值“${value.name}”吗？`))return;
    try{await this.call('/api/registry/value/delete',this.scope({path:this.selected.path,name:value.name}));await this.loadValues()}catch(error){this.notify(error)}
  }

  askName(title,value){
    return new Promise(resolve=>{this.nameResolve=resolve;this.$('name-title').textContent=title;this.$('name-input').value=value;this.nameDialog.returnValue='';this.nameDialog.showModal();this.$('name-input').select()});
  }
  finishName(){const resolve=this.nameResolve;if(!resolve)return;this.nameResolve=null;resolve(this.nameDialog.returnValue==='ok'?this.$('name-input').value.trim():null)}
  scope(extra={}){return {root:this.selected.root,...extra}}

  openValueMenu(event){const targeted=this.contextValue!==null;this.action('value-modify').hidden=this.action('value-rename').hidden=this.action('value-delete').hidden=!targeted;this.action('value-rename').disabled=this.action('value-delete').disabled=targeted&&(!this.contextValue.exists||this.contextValue.name==='');this.openMenu(this.valueMenu,event)}
  openMenu(menu,event){this.keyMenu.hidden=this.valueMenu.hidden=true;menu.hidden=false;menu.style.left=`${event.clientX}px`;menu.style.top=`${event.clientY}px`;const box=menu.getBoundingClientRect();menu.style.left=`${Math.max(6,Math.min(event.clientX,innerWidth-box.width-6))}px`;menu.style.top=`${Math.max(6,Math.min(event.clientY,innerHeight-box.height-6))}px`}
  hideMenus(){this.keyMenu.hidden=this.valueMenu.hidden=true;this.contextValue=null}
  onKey(event){
    if(event.target.closest('dialog'))return;const inTree=event.target.closest('.registry-tree');if(event.key==='F2'){event.preventDefault();inTree?this.renameKey():this.selectedValue&&this.renameValue(this.selectedValue)}else if(event.key==='Delete'){event.preventDefault();inTree?this.deleteKey():this.selectedValue&&this.deleteValue(this.selectedValue)}else if(event.key==='Enter'&&!inTree&&this.selectedValue){event.preventDefault();this.editValue(this.selectedValue)}
  }
}

function validKeyName(value){return value.length>0&&!value.includes('\\')}
function typeName(type){return TYPES.get(type)||`REG_TYPE_${type}`}
function base64Bytes(value){const text=atob(value),bytes=new Uint8Array(text.length);for(let i=0;i<text.length;i++)bytes[i]=text.charCodeAt(i);return bytes}
function bytesBase64(bytes){let text='';for(let i=0;i<bytes.length;i+=32768)text+=String.fromCharCode(...bytes.subarray(i,i+32768));return btoa(text)}
function utf16(text,terminate=true){const bytes=new Uint8Array((text.length+(terminate?1:0))*2),view=new DataView(bytes.buffer);for(let i=0;i<text.length;i++)view.setUint16(i*2,text.charCodeAt(i),true);return bytes}
function utf16Text(bytes){return new TextDecoder('utf-16le').decode(bytes).replace(/\0+$/,'')}
function hex(bytes,limit=Infinity){const shown=bytes.subarray(0,limit),text=Array.from(shown,value=>value.toString(16).padStart(2,'0').toUpperCase()).join(' ');return shown.length<bytes.length?`${text} …`:text}
function formatData(type,data,dataLength){
  const suffix=data.length<dataLength?' …':'';
  if(type===1||type===2)return `${utf16Text(data)||'(空字符串)'}${suffix}`;if(type===7)return `${utf16Text(data).split('\0').filter(Boolean).join(' ')}${suffix}`;
  if(type===4&&data.length>=4)return `0x${new DataView(data.buffer,data.byteOffset).getUint32(0,true).toString(16).padStart(8,'0')} (${new DataView(data.buffer,data.byteOffset).getUint32(0,true)})`;
  if(type===11&&data.length>=8){const value=new DataView(data.buffer,data.byteOffset).getBigUint64(0,true);return `0x${value.toString(16).padStart(16,'0')} (${value})`}
  return data.length?`${hex(data,64).replace(/ …$/,'')}${data.length>64||data.length<dataLength?' …':''}`:'(长度为零的二进制值)';
}
function editorData(type,data,base){
  if(type===1||type===2)return utf16Text(data);if(type===7)return utf16Text(data).split('\0').join('\n');
  if(type===4&&data.length>=4){const value=new DataView(data.buffer,data.byteOffset).getUint32(0,true);return base===10?String(value):value.toString(16)}
  if(type===11&&data.length>=8){const value=new DataView(data.buffer,data.byteOffset).getBigUint64(0,true);return base===10?String(value):value.toString(16)}return hex(data);
}
function parseEditorData(type,text,base){
  if(type===1||type===2)return utf16(text);if(type===7)return utf16(text.replace(/\r\n?/g,'\n').split('\n').join('\0')+'\0\0',false);
  if(type===4||type===11){const value=BigInt(base===16?`0x${text.trim().replace(/^0x/i,'')}`:text.trim()),max=type===4?0xFFFFFFFFn:0xFFFFFFFFFFFFFFFFn;if(value<0||value>max)throw new Error('数值超出类型范围');const bytes=new Uint8Array(type===4?4:8),view=new DataView(bytes.buffer);type===4?view.setUint32(0,Number(value),true):view.setBigUint64(0,value,true);return bytes}
  const value=text.replace(/[\s,]+/g,'');if(!/^(?:[0-9a-fA-F]{2})*$/.test(value))throw new Error('二进制数据必须是成对的十六进制字节');const bytes=new Uint8Array(value.length/2);for(let i=0;i<bytes.length;i++)bytes[i]=Number.parseInt(value.slice(i*2,i*2+2),16);return bytes;
}
