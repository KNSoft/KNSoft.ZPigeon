export function bindTerminalInteractions(session,onPaste,onError){
  const {terminal,host}=session;
  async function copy(){
    const text=terminal.getSelection();if(!text)return;
    try{await navigator.clipboard.writeText(text);terminal.clearSelection();terminal.focus()}catch(error){onError(error)}
  }
  async function paste(){
    try{onPaste(await navigator.clipboard.readText())}catch(error){onError(error)}
  }
  terminal.attachCustomKeyEventHandler(event=>{
    if(event.type!=='keydown')return true;
    const key=event.key.toLowerCase(),ctrl=event.ctrlKey&&!event.altKey&&!event.metaKey;
    if(ctrl&&key==='insert'){void copy();return false}
    if((ctrl&&key==='v')||(event.shiftKey&&!event.ctrlKey&&key==='insert')){void paste();return false}
    return true;
  });
  host.addEventListener('keydown',event=>{
    if(event.ctrlKey&&!event.altKey&&!event.metaKey&&event.key.toLowerCase()==='c'){
      event.preventDefault();event.stopImmediatePropagation();
      if(terminal.hasSelection()||event.shiftKey)void copy();else session.send('\x03');
    }
  },true);
  host.addEventListener('paste',event=>{
    event.preventDefault();event.stopImmediatePropagation();onPaste(event.clipboardData?.getData('text')||'');
  },true);
  host.addEventListener('contextmenu',event=>{
    event.preventDefault();terminal.clearSelection();void paste();
  });
}

export function pasteTerminal(terminal,text){
  terminal.paste(text);terminal.clearSelection();terminal.scrollToBottom();terminal.focus();
}

export function getTerminalText(terminal){
  const buffer=terminal.buffer.active,lines=[];let text='';
  for(let index=0;index<buffer.length;index++){
    const line=buffer.getLine(index);if(!line)continue;
    const value=line.translateToString(true);
    if(index&&!line.isWrapped){lines.push(text);text=''}
    text+=value;
  }
  lines.push(text);while(lines.at(-1)==='')lines.pop();return lines.join('\r\n');
}

export function exportTerminal(session){
  const invalid=/[<>:"/\\|?*\u0000-\u001f]/g,name=session.title.replace(invalid,'_').trim().replace(/[. ]+$/,'')||'Terminal';
  const stamp=new Date().toISOString().replace(/[-:]/g,'').replace(/\.\d{3}Z$/,'Z'),url=URL.createObjectURL(new Blob([getTerminalText(session.terminal)],{type:'text/plain;charset=utf-8'})),link=document.createElement('a');
  link.href=url;link.download=`${name}-${stamp}.txt`;document.body.append(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(url),0);
}
