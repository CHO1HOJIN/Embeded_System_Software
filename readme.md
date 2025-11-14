수행 작업:

블록 내 모든 슬라이스(페이지) 스캔
유효한 매핑(vsa != VSA_NONE)이 있는지 확인
매핑 일관성 체크: 역방향 매핑 검증 (virtualSlice → logicalSlice)
첫 번째 유효한 VSA에서 die 번호와 block 번호 추출
논리 주소 매핑 해제 (VSA_NONE으로 설정)
무효화된 슬라이스 개수 카운트