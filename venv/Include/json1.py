import netCDF4 as cd
import numpy as np
import csv
import json
import codecs
NC_DATA_PATH='20150101.nc'

ALL_DATA=cd.Dataset(NC_DATA_PATH)

SAVE_PATH='C:/Users/12871/Desktop/first.json'

print(ALL_DATA.variables.keys())

print(ALL_DATA.variables['water_temp'])
data_salt=ALL_DATA.variables['salinity'][:]
data_lat=ALL_DATA.variables['lat'][::]
data_lon=ALL_DATA.variables['lon'][:]
data_time=ALL_DATA.variables['time'][:]
data_all=ALL_DATA.variables['water_temp'][:]
data_depth=ALL_DATA.variables['depth'][:]
arr_salt=np.array(data_salt)
arr_all=np.array(data_all)
arr_lat=np.array(data_lat)
arr_lon=np.array(data_lon)
arr_depth=np.array(data_depth)
i=0
j=0
for lat in arr_lat:
    if lat==24.0:
        lat22=i
    if lat<=23:
        lat_min=i;#记录23°N的位置
    elif lat<=40:
        lat_max=i;#记录40°N的位置
    i+=1
for lon in arr_lon:
    if lon==128.0:
        lon22=j
    if lon<=118:
        lon_min=j;#记录118°E的位置
    elif lon<=131:
        lon_max=j;#记录131°E的位置
    j+=1
m=lat_min
n=lon_min
d=0
while True:
    while True:
        while True:

            arr_depth1=str(arr_depth[d])
            arr_lat1=str(arr_lat[m])
            arr_lon1=str(arr_lon[n])
            arr_all1=str(arr_all[0][d][m][n])
            arr_salt1=str(arr_salt[0][d][m][n])
            data = {'depth': arr_depth1, 'lat': arr_lat1, 'lon': arr_lon1, 'water_temp': arr_all1,'salinity':arr_salt1}
            with codecs.open(SAVE_PATH,'a','utf-8')as outf:
                json.dump(data,outf,ensure_ascii=False)
                outf.write('\n')
    outf.close()
ALL_DATA.close()