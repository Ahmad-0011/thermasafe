-- ============================================================
-- ThermaSafe — مخطط قاعدة البيانات (النسخة 4)
-- يشمل: سجل حرارة يومي + سجل تنبيهات (SOS ونظام) للتقارير،
--       عدة مختصين وكل واحد له عماله، محطات، حرارة مدينة.
-- شغّله في: Supabase → SQL Editor → Run
-- ⚠️ يبدأ بحذف الجداول (آمن — يعيد البناء + بيانات تجربة).
-- ============================================================

drop table if exists public.sos_alerts  cascade;
drop table if exists public.accounts    cascade;
drop table if exists public.workers     cascade;
drop table if exists public.settings    cascade;
drop table if exists public.stations    cascade;
drop table if exists public.daily_stats cascade;
drop table if exists public.alerts      cascade;
drop function if exists public.ts_update_temp(text,numeric,numeric) cascade;
drop function if exists public.ts_add_worker(text,text,text,boolean) cascade;

-- ---------- الجداول ----------
create table public.workers (
  id bigint generated always as identity primary key,
  name text not null, serial text unique not null, owner text, station text,
  day text not null default 'safe', week text not null default 'safe',
  month text not null default 'safe', status text not null default 'safe',
  temperature numeric, humidity numeric, battery numeric, temp_at timestamptz,
  dose_week numeric not null default 0, dose_month numeric not null default 0,
  created_at timestamptz not null default now()
);
create table public.accounts (
  id bigint generated always as identity primary key,
  username text unique not null, password text not null,
  role text not null check (role in ('worker','specialist')), name text not null, serial text
);
create table public.sos_alerts (
  id bigint generated always as identity primary key,
  serial text, name text, location text, lat numeric, lng numeric,
  created_at timestamptz not null default now()
);
create table public.settings (
  id int primary key default 1, temp_threshold numeric not null default 38,
  city_temp numeric not null default 40, constraint settings_single_row check (id = 1)
);
create table public.stations (
  id bigint generated always as identity primary key,
  name text not null, owner text, lat numeric, lng numeric,
  created_at timestamptz not null default now()
);
create table public.daily_stats (
  serial text, d date, sum_temp numeric not null default 0, max_temp numeric, cnt int not null default 0,
  primary key (serial, d)
);
create table public.alerts (
  id bigint generated always as identity primary key,
  serial text, kind text, level text, message text, created_at timestamptz not null default now()
);

-- ---------- RLS ----------
alter table public.workers    enable row level security;
alter table public.accounts   enable row level security;
alter table public.sos_alerts enable row level security;
alter table public.settings   enable row level security;
alter table public.stations   enable row level security;
alter table public.daily_stats enable row level security;
alter table public.alerts     enable row level security;
drop policy if exists workers_read on public.workers;      create policy workers_read on public.workers for select using (true);
drop policy if exists settings_read on public.settings;    create policy settings_read on public.settings for select using (true);
drop policy if exists stations_read on public.stations;    create policy stations_read on public.stations for select using (true);
drop policy if exists daily_read on public.daily_stats;    create policy daily_read on public.daily_stats for select using (true);
drop policy if exists alerts_read on public.alerts;        create policy alerts_read on public.alerts for select using (true);
drop policy if exists sos_read on public.sos_alerts;       create policy sos_read on public.sos_alerts for select using (true);
drop policy if exists sos_insert on public.sos_alerts;     create policy sos_insert on public.sos_alerts for insert with check (true);

-- ---------- دوال مساعدة ----------
create or replace function public.level_from_dose(d numeric) returns text language sql immutable as $$
  select case when d < 15 then 'safe' when d < 45 then 'mid' when d < 90 then 'high' else 'danger' end; $$;
create or replace function public.status_from_temp(eff numeric, thr numeric) returns text language sql immutable as $$
  select case when eff >= thr then 'danger' when eff >= thr-2 then 'high' when eff >= thr-6 then 'mid' else 'safe' end; $$;

-- ---------- RPC ----------
create or replace function public.ts_login(p_user text, p_pass text)
returns table(username text, role text, name text, serial text)
language sql security definer set search_path = public as $$
  select username, role, name, serial from public.accounts where username=p_user and password=p_pass limit 1; $$;

create or replace function public.ts_signup(p_user text, p_pass text, p_role text, p_name text, p_serial text)
returns table(username text, role text, name text, serial text)
language plpgsql security definer set search_path = public as $$
declare v_serial text; i int;
begin
  if exists (select 1 from public.accounts where username=p_user) then raise exception 'USER_EXISTS'; end if;
  v_serial := coalesce(nullif(p_serial,''), (case when p_role='worker' then 'TH-' else 'SF-' end) || floor(random()*90000+10000)::int::text);
  insert into public.accounts(username,password,role,name,serial) values (p_user,p_pass,p_role,p_name,v_serial);
  if p_role='worker' and not exists (select 1 from public.workers where serial=v_serial) then
    insert into public.workers(name,serial,owner) values (p_name,v_serial,null); end if;
  if p_role='specialist' then
    for i in 1..3 loop insert into public.workers(name,serial,owner)
      values ('عامل '||i||' - '||p_name, 'TH-'||floor(random()*900000+100000)::int::text, p_user); end loop;
    insert into public.stations(name, owner) values ('محطة ترطيب A', p_user), ('محطة ترطيب B', p_user);
  end if;
  return query select p_user, p_role, p_name, v_serial;
end; $$;

create or replace function public.ts_add_worker(p_name text, p_serial text, p_level text, p_account boolean, p_owner text)
returns void language plpgsql security definer set search_path = public as $$
declare v_serial text;
begin
  v_serial := coalesce(nullif(p_serial,''), 'TH-'||floor(random()*90000+10000)::int::text);
  insert into public.workers(name,serial,owner,day,week,month,status)
  values (p_name,v_serial,p_owner,p_level,p_level,p_level,p_level) on conflict (serial) do nothing;
  if p_account and not exists (select 1 from public.accounts where username=lower(v_serial)) then
    insert into public.accounts(username,password,role,name,serial) values (lower(v_serial),'1234','worker',p_name,v_serial); end if;
end; $$;

create or replace function public.ts_delete_worker(p_serial text)
returns void language plpgsql security definer set search_path = public as $$
begin
  delete from public.workers where serial=p_serial;
  delete from public.accounts where role='worker' and serial=p_serial;
  delete from public.daily_stats where serial=p_serial;
  delete from public.alerts where serial=p_serial;
end; $$;

create or replace function public.ts_set_threshold(p_val numeric) returns void language sql security definer set search_path=public as $$
  update public.settings set temp_threshold=p_val where id=1; $$;
create or replace function public.ts_set_city(p_val numeric) returns void language sql security definer set search_path=public as $$
  update public.settings set city_temp=p_val where id=1; $$;
create or replace function public.ts_add_station(p_name text, p_lat numeric, p_lng numeric, p_owner text) returns void language sql security definer set search_path=public as $$
  insert into public.stations(name,owner,lat,lng) values (p_name,p_owner,p_lat,p_lng); $$;
create or replace function public.ts_delete_station(p_id bigint) returns void language sql security definer set search_path=public as $$
  delete from public.stations where id=p_id; $$;
create or replace function public.ts_set_station(p_serial text, p_station text) returns void language sql security definer set search_path=public as $$
  update public.workers set station=p_station where serial=p_serial; $$;

-- تحديث الحرارة: حالة + جرعة + سجل يومي + تنبيه نظام عند بلوغ الخطر
create or replace function public.ts_update_temp(p_serial text, p_temp numeric, p_hum numeric, p_batt numeric default null)
returns void language plpgsql security definer set search_path = public as $$
declare v_thr numeric; v_city numeric; v_eff numeric; v_status text; v_old text; v_inc numeric;
begin
  select temp_threshold, city_temp into v_thr, v_city from public.settings where id=1;
  v_thr:=coalesce(v_thr,38); v_city:=coalesce(v_city,40);
  v_eff := p_temp + greatest(0, v_city-35)*0.4;
  v_status := public.status_from_temp(v_eff, v_thr);
  v_inc := greatest(0, v_eff-(v_thr-8));
  select status into v_old from public.workers where serial=p_serial;
  update public.workers set temperature=p_temp, humidity=p_hum, battery=coalesce(p_batt,battery), temp_at=now(),
     status=v_status, day=v_status, dose_week=dose_week+v_inc, dose_month=dose_month+v_inc,
     week=public.level_from_dose(dose_week+v_inc), month=public.level_from_dose(dose_month+v_inc)
   where serial=p_serial;
  if not found then
    insert into public.workers(name,serial,temperature,humidity,battery,temp_at,status,day,week,month,dose_week,dose_month)
    values ('عامل '||p_serial,p_serial,p_temp,p_hum,p_batt,now(),v_status,v_status,public.level_from_dose(v_inc),public.level_from_dose(v_inc),v_inc,v_inc);
  end if;
  insert into public.daily_stats(serial,d,sum_temp,max_temp,cnt) values (p_serial,current_date,p_temp,p_temp,1)
  on conflict (serial,d) do update set sum_temp=daily_stats.sum_temp+p_temp, max_temp=greatest(daily_stats.max_temp,p_temp), cnt=daily_stats.cnt+1;
  if v_status='danger' and (v_old is distinct from 'danger') then
    insert into public.alerts(serial,kind,level,message)
    values (p_serial,'system','danger','تنبيه النظام: بلغت حرارة العامل مستوى الخطر ('||round(p_temp,1)||'°م)');
  end if;
end; $$;

-- أي نداء SOS يُسجَّل في سجل التنبيهات تلقائياً
create or replace function public.sos_to_alert() returns trigger language plpgsql security definer set search_path=public as $$
begin
  insert into public.alerts(serial,kind,level,message)
  values (new.serial,'sos','danger','نداء استغاثة (SOS) من العامل'||coalesce(' - '||new.location,''));
  return new;
end; $$;
drop trigger if exists trg_sos_to_alert on public.sos_alerts;
create trigger trg_sos_to_alert after insert on public.sos_alerts for each row execute function public.sos_to_alert();

grant execute on function public.ts_login(text,text)                        to anon, authenticated;
grant execute on function public.ts_signup(text,text,text,text,text)        to anon, authenticated;
grant execute on function public.ts_add_worker(text,text,text,boolean,text)  to anon, authenticated;
grant execute on function public.ts_delete_worker(text)                     to anon, authenticated;
grant execute on function public.ts_set_threshold(numeric)                  to anon, authenticated;
grant execute on function public.ts_set_city(numeric)                       to anon, authenticated;
grant execute on function public.ts_add_station(text,numeric,numeric,text)   to anon, authenticated;
grant execute on function public.ts_delete_station(bigint)                  to anon, authenticated;
grant execute on function public.ts_set_station(text,text)                  to anon, authenticated;
grant execute on function public.ts_update_temp(text,numeric,numeric,numeric) to anon, authenticated;

-- ---------- Realtime ----------
do $$
begin
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='workers') then alter publication supabase_realtime add table public.workers; end if;
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='sos_alerts') then alter publication supabase_realtime add table public.sos_alerts; end if;
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='settings') then alter publication supabase_realtime add table public.settings; end if;
  if not exists (select 1 from pg_publication_tables where pubname='supabase_realtime' and schemaname='public' and tablename='stations') then alter publication supabase_realtime add table public.stations; end if;
end $$;

-- ---------- بيانات ابتدائية ----------
insert into public.settings(id, temp_threshold, city_temp) values (1, 38, 40) on conflict (id) do nothing;

insert into public.accounts(username, password, role, name, serial) values
  ('2000','1234','worker','العامل الاول','TH-20481'),
  ('2001','1234','worker','العامل الثاني','TH-20471'),
  ('2002','1234','worker','العامل الثالث','TH-20461'),
  ('1000','1234','specialist','المختص الاول','SF-1000'),
  ('1001','1234','specialist','المختص الثاني','SF-1001'),
  ('1002','1234','specialist','المختص الثالث','SF-1002')
on conflict (username) do nothing;

insert into public.workers(name, serial, owner) values
  ('العامل الاول','TH-20481','1000'), ('العامل الاول 1','TH-20482','1000'), ('العامل الاول 2','TH-20483','1000'),
  ('العامل الثاني','TH-20471','1001'), ('العامل الثاني 1','TH-20472','1001'), ('العامل الثاني 2','TH-20473','1001'),
  ('العامل الثالث','TH-20461','1002'), ('العامل الثالث 1','TH-20462','1002'), ('العامل الثالث 2','TH-20463','1002')
on conflict (serial) do nothing;

insert into public.stations(name, owner, lat, lng) values
  ('محطة ترطيب A','1000',24.7136,46.6753), ('محطة ترطيب B','1000',24.7200,46.6800),
  ('محطة ترطيب A','1001',24.7136,46.6753), ('محطة ترطيب A','1002',24.7136,46.6753);

-- سجل حرارة يومي تجريبي: 30 يوماً لكل عامل
insert into public.daily_stats(serial, d, sum_temp, max_temp, cnt)
select w.serial, gs::date, round((28+random()*14)::numeric,1)*5, round((32+random()*12)::numeric,1), 5
from public.workers w, generate_series(current_date-29, current_date, interval '1 day') gs;

-- تنبيهات تجريبية
insert into public.alerts(serial, kind, level, message, created_at) values
  ('TH-20481','sos','danger','نداء استغاثة (SOS) من العامل - وحدة الإنتاج 3', now()-interval '2 days'),
  ('TH-20481','system','danger','تنبيه النظام: بلغت حرارة العامل مستوى الخطر (41.2°م)', now()-interval '1 day'),
  ('TH-20471','system','high','تنبيه النظام: ارتفاع حرارة العامل', now()-interval '3 days');

-- ============================================================
-- تم. تأكد أن config.js يحوي رابط المشروع والمفتاح.
-- ============================================================
