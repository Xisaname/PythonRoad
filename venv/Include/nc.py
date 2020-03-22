import netCDF4 as cd
import pandas as pd
import numpy as np
import csv
import json
import codecs
NC_DATA_PATH='20150101.nc'

ALL_DATA=cd.Dataset(NC_DATA_PATH)

SAVE_PATH1='C:/Users/12871/Desktop/first.csv'
SAVE_PATH2='C:/Users/12871/Desktop/first.json'

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
data = {'depth': [arr_depth[d]], 'lat': [arr_lat[m]], 'lon': [arr_lon[n]], 'water_temp': [arr_all[0][d][m][n]],'salinity':[arr_salt[0][d][m][n]]}
data_frame = pd.DataFrame.from_dict(data)
data_frame.to_csv(SAVE_PATH1, index=True, sep=',')
while True:
    while True:
        while True:
            file=open(SAVE_PATH1,"a",newline='')
            csv_file=csv.writer(file)
            if arr_all[0][d][m][n]<-20:
                s = 0
                while True:
                    s += 1
                    if arr_all[0][d][m][n - s] > -20:
                        break
                arr_all[0][d][m][n] = 2 * arr_all[0][d][m][n - s] - arr_all[0][d][m][n - s - 1]
            if arr_salt[0][d][m][n]<-20:
                k=0
                while True:
                    k+=1
                    if arr_salt[0][d][m][n-k]>-20:
                        break
                arr_salt[0][d][m][n]=2*arr_salt[0][d][m][n-k]-arr_salt[0][d][m][n-k-1]
            arr_depth1=str(arr_depth[d])
            arr_lat1=str(arr_lat[m])
            arr_lon1=str(arr_lon[n])
            arr_all1=str(arr_all[0][d][m][n])
            arr_salt1=str(arr_salt[0][d][m][n])
            datas=[['0',arr_depth1,arr_lat1,arr_lon1,arr_all1,arr_salt1]]
            csv_file.writerows(datas)
            data1 = {'depth': arr_depth1, 'lat': arr_lat1, 'lon': arr_lon1, 'water_temp': arr_all1,
                    'salinity': arr_salt1}
            with codecs.open(SAVE_PATH2, 'a', 'utf-8')as outf:
                json.dump(data1, outf, ensure_ascii=False)
                outf.write('\n')
            if n==lon_max:
               break
            n+=1
        if m==lat_max:
           break
        m+=1
    if d==39:
        file.close();
        break
    d+=1
#print(arr_lat[1750])
ALL_DATA.close()
