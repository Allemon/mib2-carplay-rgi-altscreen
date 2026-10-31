#!/bin/sh
# V3.3 iAP2 message-array editor for dio_manager.json.
# Message sets (third argument):
#   rgi        Apple's standard 0x5200..0x5204 RGI message transport (default)
#   telephony  Communications AcceptCall/EndCall/SwapCalls 0x415B..0x415D,
#              Hold/MuteStatusUpdate 0x415F/0x4160 and the recents/favorites
#              ListUpdates 0x4170/0x4171 (rgi_native telephony/tel_hook.c)
# A set is added only when none of its IDs is present; a partly present set is refused.
set -u

ACTION=${1:-}
FILE=${2:-}
SET=${3:-rgi}
case "$ACTION" in apply|verify) ;; *)
  echo "usage: $0 {apply|verify} /path/to/dio_manager.json [rgi|telephony]" >&2
  exit 2
  ;;
esac
case "$SET" in
  rgi) TAG=V33_RGI_CONFIG; SENT_IDS="0x5200 0x5203"; RECV_IDS="0x5201 0x5202 0x5204" ;;
  telephony) TAG=TELEPHONY_CONFIG; SENT_IDS="0x415B 0x415C 0x415D 0x415F 0x4160 0x4170"; RECV_IDS="0x4171" ;;
  *)
    echo "usage: $0 {apply|verify} /path/to/dio_manager.json [rgi|telephony]" >&2
    exit 2
    ;;
esac
[ -n "$FILE" ] && [ -s "$FILE" ] || {
  echo "$TAG=FAIL reason=missing_dio path=$FILE" >&2
  exit 1
}

array_stats() {
  file=$1; key=$2; value=$3
  awk -v key="\"$key\"" -v value="\"$value\"" '
    function scan_array(s,start,   i,c,q,e) {
      for (i=start;i<=length(s);i++) {
        c=substr(s,i,1)
        if (q) { if (e) e=0; else if (c=="\\") e=1; else if (c=="\"") q=0 }
        else if (c=="\"") q=1
        else if (c=="[") depth++
        else if (c=="]") { depth--; if (depth==0) { active=0; return i } }
      }
      return 0
    }
    function count_value(s,   part,pos) {
      part=s
      while ((pos=index(part,value))>0) {
        values++; part=substr(part,pos+length(value))
      }
    }
    /^[ \t]*#/ { next }
    {
      line=$0
      if (!active && !pending) {
        key_pos=index(line,key)
        if (key_pos>0 && substr(line,key_pos+length(key)) ~ /^[ \t]*:/) {
          pending=1; arrays++; search=key_pos
        }
      } else search=1
      if (pending && !active) {
        open_rel=index(substr(line,search),"[")
        if (open_rel>0) { start=search+open_rel-1; active=1; pending=0; depth=0 }
      } else if (active) start=1
      if (active) {
        close_at=scan_array(line,start)
        if (close_at>0) count_value(substr(line,start,close_at-start+1))
        else count_value(substr(line,start))
      }
    }
    END { print arrays+0, values+0 }
  ' "$file"
}

state_of() {
  file=$1; present=0; absent=0; bad=0; total=0
  for spec in $(for v in $SENT_IDS; do echo "MessagesSentByAccessory:$v"; done
                for v in $RECV_IDS; do echo "MessagesReceivedFromDevice:$v"; done)
  do
    key=${spec%%:*}; value=${spec#*:}
    total=$((total+1))
    set -- $(array_stats "$file" "$key" "$value")
    arrays=${1:-0}; values=${2:-0}
    [ "$arrays" -eq 1 ] || bad=1
    case "$values" in
      0) absent=$((absent+1)) ;;
      1) present=$((present+1)) ;;
      *) bad=1 ;;
    esac
  done
  if [ "$bad" -ne 0 ]; then echo INVALID
  elif [ "$present" -eq "$total" ]; then echo COMPLETE
  elif [ "$absent" -eq "$total" ]; then echo ABSENT
  else echo MIXED
  fi
}

verify_complete() { [ "$(state_of "$1")" = COMPLETE ]; }

if [ "$ACTION" = verify ]; then
  state=$(state_of "$FILE")
  [ "$state" = COMPLETE ] && {
    echo "$TAG=PASS state=COMPLETE"
    exit 0
  }
  echo "$TAG=FAIL state=$state" >&2
  exit 1
fi

state=$(state_of "$FILE")
case "$state" in
  COMPLETE)
    echo "$TAG=UNCHANGED state=COMPLETE"
    exit 0
    ;;
  ABSENT) ;;
  *)
    echo "$TAG=REFUSED state=$state reason=ambiguous_existing_message_set" >&2
    exit 1
    ;;
esac

TMP="$FILE.v33-rgi.$$"
rm -f "$TMP" 2>/dev/null || true
trap 'rm -f "$TMP" 2>/dev/null || true' 0
trap 'exit 130' 1 2 15

awk -v sent_ids="$SENT_IDS" -v recv_ids="$RECV_IDS" '
  function find_close(s,start,   i,c,q,e) {
    for (i=start;i<=length(s);i++) {
      c=substr(s,i,1)
      if (q) { if (e) e=0; else if (c=="\\") e=1; else if (c=="\"") q=0 }
      else if (c=="\"") { q=1; has_element=1 }
      else if (c=="[") depth++
      else if (c=="]") { depth--; if (depth==0) return i }
    }
    return 0
  }
  function quoted(list,   n,i,s,id) {
    n=split(list,id," "); s=""
    for (i=1;i<=n;i++) s=s (i>1 ? ", " : "") "\"" id[i] "\""
    return s
  }
  function leading_ws(s) { match(s,/^[ \t]*/); return substr(s,1,RLENGTH) }
  function nonblank(s) { return s ~ /[^ \t]/ }
  function element_line(s,first,   part,p) {
    part=s
    if (first) { p=index(part,"["); if (p>0) part=substr(part,p+1) }
    return index(part,"\"")>0
  }
  function add_comma(s,   t,ws) {
    match(s,/[ \t]*$/); ws=substr(s,RSTART); t=substr(s,1,RSTART-1)
    if (t !~ /,$/) t=t ","
    return t ws
  }
  function flush_multiline(close_at,values,   prefix,suffix,close_ws,content_n,last,i,indent,n,v) {
    prefix=substr(buf[buf_n],1,close_at-1)
    suffix=substr(buf[buf_n],close_at)
    close_ws=leading_ws(buf[buf_n])
    if (nonblank(prefix)) {
      buf[buf_n]=prefix; content_n=buf_n; close_line=close_ws suffix
    } else {
      content_n=buf_n-1; close_line=buf[buf_n]
    }
    last=0
    for (i=content_n;i>=1;i--) if (element_line(buf[i],i==1)) { last=i; break }
    if (last>0) {
      buf[last]=add_comma(buf[last]); indent=leading_ws(buf[last])
      if (last==1) indent=close_ws "    "
    } else indent=close_ws "    "
    for (i=1;i<=content_n;i++) print buf[i]
    n=split(values,new_value," ")
    for (i=1;i<=n;i++) {
      v=new_value[i]
      if (i<n) print indent "\"" v "\","
      else print indent "\"" v "\""
    }
    print close_line
    delete buf; delete new_value; buf_n=0
  }
  {
    line=$0
    if (active && multiline) {
      buf[++buf_n]=line
      if (line !~ /^[ \t]*#/) {
        close_at=find_close(line,1)
        if (close_at>0) {
          values=(mode=="sent") ? sent_ids : recv_ids
          flush_multiline(close_at,values)
          active=0; multiline=0
          if (mode=="sent") sent_done++; else recv_done++
          mode=""
        }
      }
      next
    }
    if (line ~ /^[ \t]*#/) { print line; next }

    if (!active && !pending) {
      sent_pos=index(line,"\"MessagesSentByAccessory\"")
      recv_pos=index(line,"\"MessagesReceivedFromDevice\"")
      if (sent_pos>0 && substr(line,sent_pos+length("\"MessagesSentByAccessory\"")) ~ /^[ \t]*:/) {
        mode="sent"; pending=1; search=sent_pos
      } else if (recv_pos>0 && substr(line,recv_pos+length("\"MessagesReceivedFromDevice\"")) ~ /^[ \t]*:/) {
        mode="recv"; pending=1; search=recv_pos
      }
    } else search=1

    if (pending && !active) {
      open_rel=index(substr(line,search),"[")
      if (open_rel>0) {
        start=search+open_rel-1; active=1; pending=0; depth=0; has_element=0
        close_at=find_close(line,start)
        if (close_at>0) {
          values=(mode=="sent") ? quoted(sent_ids) : quoted(recv_ids)
          addition=has_element ? ", " values : values
          line=substr(line,1,close_at-1) addition substr(line,close_at)
          active=0
          if (mode=="sent") sent_done++; else recv_done++
          mode=""
        } else {
          multiline=1; buf[++buf_n]=line; next
        }
      }
    }
    print line
  }
  END { if (sent_done!=1 || recv_done!=1 || active || pending) exit 2 }
' "$FILE" > "$TMP" || {
  echo "$TAG=FAIL reason=patch_failed" >&2
  exit 1
}

verify_complete "$TMP" || {
  echo "$TAG=FAIL reason=post_patch_verify" >&2
  exit 1
}
chmod 644 "$TMP" || exit 1
mv "$TMP" "$FILE" || exit 1
trap - 0 1 2 15
echo "$TAG=PASS state=PATCHED"
